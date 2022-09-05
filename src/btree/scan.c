/*-------------------------------------------------------------------------
 *
 * scan.c
 *		Routines for sequential scan of orioledb B-tree
 *
 * Copyright (c) 2021-2022, Oriole DB Inc.
 *
 * IDENTIFICATION
 *	  contrib/orioledb/src/btree/scan.c
 *
 * ALGORITHM
 *
 *		The big picture algorithm of sequential scan is following.
 *		1. Scan all the internal pages with level == 1. The total amount of
 *		   internal pages are expected to be small. So, it should be OK to
 *		   scan them in logical order.
 *		   1.1. Immediately scan children's leaves and return their contents.
 *		   1.2. Edge cases are handled using iterators. They are expected to
 *		   be very rare.
 *		   1.3. Collect on-disk downlinks into an array together with CSN at
 *		   the moment of the corresponding internal page read.
 *		2. Ascending sort array of downlinks providing as sequential access
 *		   pattern as possible.
 *		3. Scan sorted downlink and apply the corresponding CSN.
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "orioledb.h"

#include "btree/btree.h"
#include "btree/find.h"
#include "btree/io.h"
#include "btree/iterator.h"
#include "btree/page_chunks.h"
#include "btree/scan.h"
#include "btree/undo.h"
#include "tuple/slot.h"
#include "utils/sampling.h"
#include "utils/stopevent.h"
#include "tableam/handler.h"

#include "miscadmin.h"

typedef enum
{
	BTreeSeqScanInMemory,
	BTreeSeqScanDisk,
	BTreeSeqScanFinished
} BTreeSeqScanStatus;

typedef struct
{
	uint64		downlink;
	CommitSeqNo csn;
} BTreeSeqScanDiskDownlink;

struct BTreeSeqScan
{
	BTreeDescr *desc;

	char		leafImg[ORIOLEDB_BLCKSZ];
	char		histImg[ORIOLEDB_BLCKSZ];

	CommitSeqNo snapshotCsn;
	OBTreeFindPageContext context;
	OFixedKey	prevHikey;
	BTreeLocationHint hint;

	BTreePageItemLocator intLoc;

	/*
	 * The page offset we started with according to `prevHikey`;
	 */
	OffsetNumber intStartOffset;

	BTreePageItemLocator leafLoc;

	bool		haveHistImg;
	BTreePageItemLocator histLoc;

	BTreeSeqScanStatus status;
	MemoryContext mctx;

	BTreeSeqScanDiskDownlink *diskDownlinks;
	int64		downlinksCount;
	int64		downlinkIndex;
	int64		allocatedDownlinks;

	BTreeIterator *iter;
	OTuple		iterEnd;

	/*
	 * Number of the last completed checkpoint when scan was started.  We need
	 * on-disk pages of this checkpoint to be not overriden until scan
	 * finishes.  This means we shouldn't start using free blocks of later
	 * checkpoints before this scan is finished.
	 */
	uint32		checkpointNumber;

	BTreeMetaPage *metaPageBlkno;
	dlist_node	listNode;

	OFixedKey	nextKey;

	bool		needSampling;
	BlockSampler sampler;
	BlockNumber samplingNumber;
	BlockNumber samplingNext;

	BTreeSeqScanCallbacks *cb;
	void	   *arg;
	bool		isSingleLeafPage;	/* Scan couldn't read first internal page */
	OFixedKey	keyRangeLow,
				keyRangeHigh;
	bool		firstPageIsLoaded;

	/* Private parallel worker info in a backend */
	ParallelOScanDesc 	poscan;
	bool 				isLeader;
	int 				workerNumber;
};

static dlist_head listOfScans = DLIST_STATIC_INIT(listOfScans);

static void scan_make_iterator(BTreeSeqScan *scan, OTuple startKey, OTuple keyRangeHigh);
static void get_next_key(BTreeSeqScan *scan, BTreePageItemLocator *intLoc, OFixedKey *nextKey, Page page);

static void
load_first_historical_page(BTreeSeqScan *scan)
{
	BTreePageHeader *header = (BTreePageHeader *) scan->leafImg;
	Pointer		key = NULL;
	BTreeKeyType kind = BTreeKeyNone;
	OFixedKey	lokey,
			   *lokeyPtr = &lokey;
	OFixedKey	hikey;

	scan->haveHistImg = false;
	if (!COMMITSEQNO_IS_NORMAL(scan->snapshotCsn))
		return;

	if (!O_PAGE_IS(scan->leafImg, RIGHTMOST))
		copy_fixed_hikey(scan->desc, &hikey, scan->leafImg);
	else
		O_TUPLE_SET_NULL(hikey.tuple);
	O_TUPLE_SET_NULL(lokey.tuple);

	while (COMMITSEQNO_IS_NORMAL(header->csn) &&
		   header->csn >= scan->snapshotCsn)
	{
		if (!UNDO_REC_EXISTS(header->undoLocation))
		{
			ereport(ERROR,
					(errcode(ERRCODE_SNAPSHOT_TOO_OLD),
					 errmsg("snapshot too old")));
		}

		(void) get_page_from_undo(scan->desc, header->undoLocation, key, kind,
								  scan->histImg, NULL, NULL, NULL,
								  lokeyPtr, &hikey.tuple);

		if (!O_PAGE_IS(scan->histImg, RIGHTMOST))
			copy_fixed_hikey(scan->desc, &hikey, scan->histImg);
		else
			O_TUPLE_SET_NULL(hikey.tuple);

		scan->haveHistImg = true;
		header = (BTreePageHeader *) scan->histImg;
		if (!O_TUPLE_IS_NULL(lokey.tuple))
		{
			key = (Pointer) &lokey.tuple;
			kind = BTreeKeyNonLeafKey;
			lokeyPtr = NULL;
		}
	}

	if (!scan->haveHistImg)
		return;

	if (!O_TUPLE_IS_NULL(lokey.tuple))
	{
		(void) btree_page_search(scan->desc, scan->histImg,
								 (Pointer) &lokey.tuple,
								 BTreeKeyNonLeafKey, NULL,
								 &scan->histLoc);
		(void) page_locator_find_real_item(scan->histImg, NULL, &scan->histLoc);
	}
	else
	{
		BTREE_PAGE_LOCATOR_FIRST(scan->histImg, &scan->histLoc);
	}

}

static void
load_next_historical_page(BTreeSeqScan *scan)
{
	BTreePageHeader *header = (BTreePageHeader *) scan->leafImg;
	OFixedKey	prevHikey;

	copy_fixed_hikey(scan->desc, &prevHikey, scan->histImg);

	while (COMMITSEQNO_IS_NORMAL(header->csn) &&
		   header->csn >= scan->snapshotCsn)
	{
		if (!UNDO_REC_EXISTS(header->undoLocation))
		{
			ereport(ERROR,
					(errcode(ERRCODE_SNAPSHOT_TOO_OLD),
					 errmsg("snapshot too old")));
		}
		(void) get_page_from_undo(scan->desc, header->undoLocation,
								  (Pointer) &prevHikey.tuple, BTreeKeyNonLeafKey,
								  scan->histImg, NULL, NULL, NULL,
								  NULL, NULL);
		header = (BTreePageHeader *) scan->histImg;
	}
	BTREE_PAGE_LOCATOR_FIRST(scan->histImg, &scan->histLoc);
}

static inline void
set_first_page_loaded(BTreeSeqScan *scan)
{
	if(scan->poscan)
		scan->poscan->flags |= O_PARALLEL_FIRST_PAGE_LOADED;
	else
		scan->firstPageIsLoaded = true;
}

static inline bool
is_first_page_loaded(BTreeSeqScan *scan)
{
	return scan->poscan ? ((scan->poscan->flags & O_PARALLEL_FIRST_PAGE_LOADED) != 0) : scan->firstPageIsLoaded;
}

static inline OTuple
int_page_hikey(BTreeSeqScan *scan, Page page)
{
	OTuple		res;

	if (is_first_page_loaded(scan) && !O_PAGE_IS(page, RIGHTMOST))
		return page_get_hikey(page);
	else
	{
		O_TUPLE_SET_NULL(res);
		return res;
	}
}

/*
 * Loads next internal page.
 *
 * Pointers to a page are provided explicitly to make function compatible with parallel or plain seqscan.
 * In case of parallel scan the caller should hold a lock preventing the other workers from modifying
 * a page in a shared state and updating prevHikey.
 *
 * page_in is provided to get hikey from it, it is not modified. Result page is loaded to page_out.
 * They could provided different only in case we prefetch next page into another image than prevoius.
 * startOffset could be output explicitly for the same purpose to be tracked on per-page basis.
 *
 * In cases that don't need prefetch provide same page_in and page_out and ignore startOffsetOut.
 */
static bool
load_next_internal_page(BTreeSeqScan *scan, Page page_in, OFixedShmemKey *prevHikey,
						Page page_out, OffsetNumber *startOffsetOut)
{
	bool		has_next = false;

	elog(DEBUG3, "load_next_internal_page");
	scan->context.flags |= BTREE_PAGE_FIND_DOWNLINK_LOCATION;

	if (!O_TUPLE_IS_NULL(int_page_hikey(scan, page_in)))
	{
		copy_fixed_key(scan->desc, &scan->prevHikey, int_page_hikey(scan, page_in));
		find_page(&scan->context, &scan->prevHikey.tuple, BTreeKeyNonLeafKey, 1);
	}
	else
	{
		O_TUPLE_SET_NULL(scan->prevHikey.tuple);
		find_page(&scan->context, NULL, BTreeKeyNone, 1);
	}

	set_first_page_loaded(scan);

	/* In case of parallel scan copy page image into shared state and update previous shared state page Hikey */
	if(page_out != scan->context.img)
	{
		Assert(scan->poscan);
		memcpy(page_out, scan->context.img, ORIOLEDB_BLCKSZ);
		copy_fixed_shmem_key(scan->desc, prevHikey, scan->prevHikey.tuple);
	}

	if (PAGE_GET_LEVEL(page_out) == 1)
	{
		/*
		 * Check if the left bound of the found keyrange corresponds to the
		 * previous hikey.  Otherwise, use iterator to correct the situation.
		 */
		scan->intLoc = scan->context.items[scan->context.index].locator;
		scan->intStartOffset = BTREE_PAGE_LOCATOR_GET_OFFSET(page_out, &scan->intLoc);
		*startOffsetOut = scan->intStartOffset;
		if (!O_TUPLE_IS_NULL(scan->prevHikey.tuple))
		{
			OTuple		intTup;

			if (scan->intStartOffset > 0)
				BTREE_PAGE_READ_INTERNAL_TUPLE(intTup, page_out, &scan->intLoc);
			else
				intTup = scan->context.lokey.tuple;

			if (o_btree_cmp(scan->desc,
							&scan->prevHikey.tuple, BTreeKeyNonLeafKey,
							&intTup, BTreeKeyNonLeafKey) != 0)
			{
				get_next_key(scan, &scan->intLoc, &scan->keyRangeHigh, page_out);
				elog(DEBUG3, "scan_make_iterator");
				scan_make_iterator(scan, scan->prevHikey.tuple, scan->keyRangeHigh.tuple);
			}
		}
		has_next = true;
	}
	else
	{
		Assert(PAGE_GET_LEVEL(page_out) == 0);
		memcpy(scan->leafImg, page_out, ORIOLEDB_BLCKSZ);
		BTREE_PAGE_LOCATOR_FIRST(scan->leafImg, &scan->leafLoc);
		scan->hint.blkno = scan->context.items[0].blkno;
		scan->hint.pageChangeCount = scan->context.items[0].pageChangeCount;
		BTREE_PAGE_LOCATOR_SET_INVALID(&scan->intLoc);
		O_TUPLE_SET_NULL(scan->nextKey.tuple);
		load_first_historical_page(scan);
		has_next = false;
	}
	return has_next;
}

static void
add_on_disk_downlink(BTreeSeqScan *scan, uint64 downlink, CommitSeqNo csn)
{
	if (scan->downlinksCount >= scan->allocatedDownlinks)
	{
		scan->allocatedDownlinks *= 2;
		scan->diskDownlinks = (BTreeSeqScanDiskDownlink *) repalloc_huge(scan->diskDownlinks,
																		 sizeof(scan->diskDownlinks[0]) * scan->allocatedDownlinks);
	}
	scan->diskDownlinks[scan->downlinksCount].downlink = downlink;
	scan->diskDownlinks[scan->downlinksCount].csn = csn;
	scan->downlinksCount++;
}

static int
cmp_downlinks(const void *p1, const void *p2)
{
	uint64		d1 = ((BTreeSeqScanDiskDownlink *) p1)->downlink;
	uint64		d2 = ((BTreeSeqScanDiskDownlink *) p2)->downlink;

	if (d1 < d2)
		return -1;
	else if (d1 == d2)
		return 0;
	else
		return 1;
}

static void
switch_to_disk_scan(BTreeSeqScan *scan)
{
	scan->status = BTreeSeqScanDisk;
	BTREE_PAGE_LOCATOR_SET_INVALID(&scan->leafLoc);
	qsort(scan->diskDownlinks,
		  scan->downlinksCount,
		  sizeof(scan->diskDownlinks[0]),
		  cmp_downlinks);
}

/*
 * Make an interator to read the key range from `startKey` to the next
 * downlink or hikey of internal page hikey if we're considering the last
 * downlink.
 */
static void
scan_make_iterator(BTreeSeqScan *scan, OTuple keyRangeLow, OTuple keyRangeHigh)
{
	MemoryContext mctx;

	mctx = MemoryContextSwitchTo(scan->mctx);
	if (!O_TUPLE_IS_NULL(keyRangeLow))
		scan->iter = o_btree_iterator_create(scan->desc, &keyRangeLow, BTreeKeyNonLeafKey,
											 scan->snapshotCsn,
											 ForwardScanDirection);
	else
		scan->iter = o_btree_iterator_create(scan->desc, NULL, BTreeKeyNone,
											 scan->snapshotCsn,
											 ForwardScanDirection);
	MemoryContextSwitchTo(mctx);

	BTREE_PAGE_LOCATOR_SET_INVALID(&scan->leafLoc);
	scan->haveHistImg = false;
	scan->iterEnd = keyRangeHigh;
}

static inline void
print_debug_page_loaded(BTreeSeqScan *scan, int slot, int custom)
{
#ifdef O_PARALLEL_DEBUG
	scan->poscan->cur_int_pageno++;
	scan->poscan->intPage[slot].pageno = poscan->cur_int_pageno;
	elog(DEBUG3, "(%d) Page %d loaded to slot %d", custom, scan->poscan->cur_int_pageno, slot);
#endif
}

/* Output item downlink and key using provided page and current locator from BTreeSeqScan */
static void
get_current_downlink_key(BTreeSeqScan *scan, OFixedKey *curKey, uint64 *downlink, Page page)
{
	BTreeNonLeafTuphdr  *tuphdr;
	OTuple 				 tuple;

	STOPEVENT(STOPEVENT_STEP_DOWN, btree_downlink_stopevent_params(scan->desc,
			  page, &scan->intLoc));

	BTREE_PAGE_READ_INTERNAL_ITEM(tuphdr, tuple, page, &scan->intLoc);
	*downlink = tuphdr->downlink;

	if (BTREE_PAGE_LOCATOR_GET_OFFSET(page, &scan->intLoc) != scan->intStartOffset)
			copy_fixed_key(scan->desc, curKey, tuple);
	else if (!O_PAGE_IS(page, LEFTMOST))
		{
			Assert(!O_TUPLE_IS_NULL(scan->prevHikey.tuple));
			copy_fixed_key(scan->desc, curKey, scan->prevHikey.tuple);
		}
	else
		{
			Assert(O_TUPLE_IS_NULL(scan->prevHikey.tuple));
			clear_fixed_key(curKey);
		}
}

/* Output next key and locator on a provided internal page */
static void
get_next_key(BTreeSeqScan *scan, BTreePageItemLocator *intLoc, OFixedKey *nextKey, Page page)
{
	BTREE_PAGE_LOCATOR_NEXT(page, intLoc);
	if (BTREE_PAGE_LOCATOR_IS_VALID(page, intLoc))
		copy_fixed_page_key(scan->desc, nextKey, page, intLoc);
	else if (!O_PAGE_IS(page, RIGHTMOST))
		copy_fixed_hikey(scan->desc, nextKey, page);
	else
		clear_fixed_key(nextKey);
}

/*
 * Gets the next downlink with it's keyrange (low and high keys of the
 * keyrange).
 *
 * Returns true on success.  False result can be caused by one of three reasons:
 * 1) The rightmost internal page is processed;
 * 2) There is just single leaf page in the tree (and it's loaded into
 *    scan->context.img);
 * 3) There is scan->iter to be processed before we can get downlinks from the
 *    current internal page.
 */
static bool
get_next_downlink(BTreeSeqScan *scan, uint64 *downlink,
				  OFixedKey *keyRangeLow, OFixedKey *keyRangeHigh)
{
	ParallelOScanDesc poscan = scan->poscan;

	/* Sequential scan*/
	if(!poscan)
	{
		bool        pageIsLoaded = scan->firstPageIsLoaded;
		while (true)
		{
			/* Try to load next internal page if needed */
			if (!pageIsLoaded)
			{
				OffsetNumber 	unused;

				if (!load_next_internal_page(scan, scan->context.img, NULL, scan->context.img, &unused))
				{
					/* first page only */
					Assert(O_PAGE_IS(scan->context.img, LEFTMOST));
					scan->isSingleLeafPage = true;
					clear_fixed_key(keyRangeLow);
					clear_fixed_key(keyRangeHigh);
					return false;
				}

				if (scan->iter)
					return false;
			}

			if (BTREE_PAGE_LOCATOR_IS_VALID(scan->context.img, &scan->intLoc))
			{
				get_current_downlink_key(scan, keyRangeLow, downlink, scan->context.img);
				/*
				 * construct fixed hikey of internal item and get next internal
				 * locator
				 */
				get_next_key(scan, &scan->intLoc, keyRangeHigh, scan->context.img);
				return true;
			}

		if (O_PAGE_IS(scan->context.img, RIGHTMOST))
			return false;

		pageIsLoaded = false;
		}
	}
	/* Parallel sequential scan */
	else
	{
		bool pageIsLoaded = is_first_page_loaded(scan);

		SpinLockAcquire(&poscan->intpageAccess);
		while (true)
		{
			/* Try to load next internal page if needed */
			if (!pageIsLoaded)
			{
				if (poscan->intPage[NEXT_PAGE].loaded)
				{
					/*
					 * Rotate current page to next prefetched one.
					 * Next page is not expected to be loaded without current.
					 */
					poscan->flags ^= (O_PARALLEL_CURRENT_PAGE | O_PARALLEL_NEXT_PAGE);
					elog(DEBUG3, "Switch current slot %d -> %d", CUR_PAGE, NEXT_PAGE);
				}
				else
				{
					/* First page only */
					poscan->intPage[CUR_PAGE].loaded =
						load_next_internal_page(scan, poscan->intPage[CUR_PAGE].img,
												&poscan->intPage[CUR_PAGE].prevHikey,
												poscan->intPage[CUR_PAGE].img,
												&poscan->intPage[CUR_PAGE].startOffset);
					poscan->intPage[NEXT_PAGE].imgReadCsn = scan->context.imgReadCsn;
					if (!poscan->intPage[CUR_PAGE].loaded)
					{
						if(O_PAGE_IS(poscan->intPage[CUR_PAGE].img, LEFTMOST))
						{
							elog(DEBUG3, "Got single leaf page in parallel scan");
							poscan->flags |= O_PARALLEL_IS_SINGLE_LEAF_PAGE;
							SpinLockRelease(&poscan->intpageAccess);
							clear_fixed_key(keyRangeLow);
							clear_fixed_key(keyRangeHigh);
							return false;
						}
						else
							elog(ERROR, "Could not load int page into current shared slot %d. Slots: %s:%s", CUR_PAGE,
									poscan->intPage[0].loaded ? "full" : "empty", poscan->intPage[1].loaded ? "full" : "empty");
					}
					print_debug_page_loaded(scan, CUR_PAGE, 1);
				}

				if (!poscan->intPage[NEXT_PAGE].loaded && !O_PAGE_IS(poscan->intPage[CUR_PAGE].img, RIGHTMOST))
				{
					/* Prefetch next page. NB: we use current page image as a base for curHikey calculation in
					 * load_next_internal_page */
					poscan->intPage[NEXT_PAGE].loaded =
						load_next_internal_page(scan, poscan->intPage[CUR_PAGE].img,
												&poscan->intPage[NEXT_PAGE].prevHikey,
												poscan->intPage[NEXT_PAGE].img,
												&poscan->intPage[NEXT_PAGE].startOffset);
					poscan->intPage[NEXT_PAGE].imgReadCsn = scan->context.imgReadCsn;
					if(poscan->intPage[NEXT_PAGE].loaded)
					{
#ifdef USE_ASSERT_CHECKING
						OTuple curkey = int_page_hikey(scan, poscan->intPage[CUR_PAGE].img);
						Assert(o_btree_cmp(scan->desc, &poscan->intPage[NEXT_PAGE].prevHikey.fixed.tuple,  BTreeKeyNonLeafKey, &curkey, BTreeKeyNonLeafKey) == 0); 
#endif
						print_debug_page_loaded(scan, NEXT_PAGE, 2);
					}
				}

				if (scan->iter)
				{
					SpinLockRelease(&poscan->intpageAccess);
					return false;
				}

				/* Push offset for new loaded page into shared state */
				scan->context.imgReadCsn = poscan->intPage[CUR_PAGE].imgReadCsn;
				scan->intStartOffset = poscan->intPage[CUR_PAGE].startOffset;
				poscan->offset = poscan->intPage[CUR_PAGE].startOffset;
				elog(DEBUG3, "Worker %d loaded intpage, page %d%s%s from slot %d, offset %d", scan->workerNumber,
					 poscan->intPage[CUR_PAGE].pageno,
					 O_PAGE_IS(poscan->intPage[CUR_PAGE].img, LEFTMOST) ? " LEFTMOST" : "",
					 O_PAGE_IS(poscan->intPage[CUR_PAGE].img, RIGHTMOST) ? " RIGHTMOST" : "", CUR_PAGE, poscan->offset);
			}

			if(poscan->flags & O_PARALLEL_IS_SINGLE_LEAF_PAGE)
				return false;

			/* Get locator from shared state internal item page offset */
			BTREE_PAGE_OFFSET_GET_LOCATOR(poscan->intPage[CUR_PAGE].img, poscan->offset, &scan->intLoc);
			elog(DEBUG3, "Worker %d get page %d, offset %d, item %s", scan->workerNumber,
				 poscan->intPage[CUR_PAGE].pageno, poscan->offset,
				 BTREE_PAGE_LOCATOR_IS_VALID(poscan->intPage[CUR_PAGE].img, &scan->intLoc) ? "valid" : "invalid");

			if (BTREE_PAGE_LOCATOR_IS_VALID(poscan->intPage[CUR_PAGE].img, &scan->intLoc)) /* inside int page */
			{
				/* Fetch previous page hikey from shared state */
				if(O_TUPLE_IS_NULL(poscan->intPage[CUR_PAGE].prevHikey.fixed.tuple))
					clear_fixed_key(&scan->prevHikey);
				else
				{
					scan->prevHikey.tuple.data = (Pointer) &poscan->intPage[CUR_PAGE].prevHikey.fixed.fixedData;
					scan->prevHikey.tuple.formatFlags = poscan->intPage[CUR_PAGE].prevHikey.fixed.tuple.formatFlags;
				}

				get_current_downlink_key(scan, keyRangeLow, downlink, poscan->intPage[CUR_PAGE].img);
				/* Get next internal page locator and next internal item hikey */
				get_next_key(scan, &scan->intLoc, keyRangeHigh, poscan->intPage[CUR_PAGE].img);

				/* Push next internal item page offset into shared state */
				poscan->offset = BTREE_PAGE_LOCATOR_GET_OFFSET(poscan->intPage[CUR_PAGE].img, &scan->intLoc);
				SpinLockRelease(&poscan->intpageAccess);
				return true;
			}

			if (O_PAGE_IS(poscan->intPage[CUR_PAGE].img, RIGHTMOST))
			{
				SpinLockRelease(&poscan->intpageAccess);
				elog(DEBUG3, "Worker %d finish int pages at page %d%s%s, offset %d", scan->workerNumber,
					 poscan->intPage[CUR_PAGE].pageno,
					 O_PAGE_IS(poscan->intPage[CUR_PAGE].img, LEFTMOST) ? " LEFTMOST" : "",
					 O_PAGE_IS(poscan->intPage[CUR_PAGE].img, RIGHTMOST) ? " RIGHTMOST" : "", poscan->offset);
				return false;
			}

			pageIsLoaded = false; 							/* Try to load next page */
			poscan->intPage[CUR_PAGE].loaded = false;	/* Mark shared page slot as free */
			elog(DEBUG3, "Worker %d completed int page %d in slot %d", scan->workerNumber,
				 poscan->intPage[CUR_PAGE].pageno, CUR_PAGE);
		}
	}
}

/*
 * Checks if loaded leaf page matches downlink of internal page.  Makes iterator
 * to read the considered key range if check failed.
 *
 * Hikey of leaf page should match to next downlink or internal page hikey if
 * we're considering the last downlink.
 */
static void
check_in_memory_leaf_page(BTreeSeqScan *scan, OTuple keyRangeLow, OTuple keyRangeHigh)
{
	OTuple		leafHikey;
	bool		result = false;

	if (!O_PAGE_IS(scan->leafImg, RIGHTMOST))
		BTREE_PAGE_GET_HIKEY(leafHikey, scan->leafImg);
	else
		O_TUPLE_SET_NULL(leafHikey);

	if (O_TUPLE_IS_NULL(keyRangeHigh) && O_TUPLE_IS_NULL(leafHikey))
		return;

	if (O_TUPLE_IS_NULL(keyRangeHigh) || O_TUPLE_IS_NULL(leafHikey))
	{
		result = true;
	}
	else
	{
		if (o_btree_cmp(scan->desc,
						&keyRangeHigh, BTreeKeyNonLeafKey,
						&leafHikey, BTreeKeyNonLeafKey) != 0)
			result = true;
	}

	if (result)
	{
		elog(DEBUG3, "scan_make_iterator 2");
		scan_make_iterator(scan, keyRangeLow, keyRangeHigh);
	}
}


/*
 * Interates the internal page till we either:
 *  - Successfully read the next in-memory leaf page;
 *  - Made an iterator to read key range, which belongs to current downlink;
 *  - Reached the end of internal page.
 */
static bool
iterate_internal_page(BTreeSeqScan *scan)
{
	uint64		downlink = 0;

	while (get_next_downlink(scan, &downlink, &scan->keyRangeLow, &scan->keyRangeHigh))
	{
		bool		valid_downlink = true;

		if (scan->cb && scan->cb->isRangeValid)
			valid_downlink = scan->cb->isRangeValid(scan->keyRangeLow.tuple, scan->keyRangeHigh.tuple,
													scan->arg);
		else if (scan->needSampling)
		{
			if (scan->samplingNumber < scan->samplingNext)
			{
				valid_downlink = false;
			}
			else
			{
				if (BlockSampler_HasMore(scan->sampler))
					scan->samplingNext = BlockSampler_Next(scan->sampler);
				else
					scan->samplingNext = InvalidBlockNumber;
			}
			scan->samplingNumber++;
		}

		if (valid_downlink)
		{
			if (DOWNLINK_IS_ON_DISK(downlink))
			{
				add_on_disk_downlink(scan, downlink, scan->context.imgReadCsn);
			}
			else if (DOWNLINK_IS_IN_MEMORY(downlink))
			{
				ReadPageResult result;

				result = o_btree_try_read_page(scan->desc,
											   DOWNLINK_GET_IN_MEMORY_BLKNO(downlink),
											   DOWNLINK_GET_IN_MEMORY_CHANGECOUNT(downlink),
											   scan->leafImg,
											   scan->context.imgReadCsn,
											   NULL,
											   BTreeKeyNone,
											   NULL,
											   NULL);

				if (result == ReadPageResultOK)
				{
					check_in_memory_leaf_page(scan, scan->keyRangeLow.tuple, scan->keyRangeHigh.tuple);
					if (scan->iter)
						return true;

					scan->hint.blkno = DOWNLINK_GET_IN_MEMORY_BLKNO(downlink);
					scan->hint.pageChangeCount = DOWNLINK_GET_IN_MEMORY_CHANGECOUNT(downlink);
					BTREE_PAGE_LOCATOR_FIRST(scan->leafImg, &scan->leafLoc);
					O_TUPLE_SET_NULL(scan->nextKey.tuple);
					load_first_historical_page(scan);
					return true;
				}
				else
				{
					scan_make_iterator(scan, scan->keyRangeLow.tuple, scan->keyRangeHigh.tuple);
					Assert(scan->iter);
					return true;
				}
			}
			else if (DOWNLINK_IS_IN_IO(downlink))
			{
				/*
				 * Downlink has currently IO in-progress.  Wait for IO
				 * completion and refind this downlink.
				 */
				int			ionum = DOWNLINK_GET_IO_LOCKNUM(downlink);

				wait_for_io_completion(ionum);

				elog(DEBUG3, "DOWNLINK_IS_IN_IO");
				scan_make_iterator(scan, scan->keyRangeLow.tuple, scan->keyRangeHigh.tuple);
				Assert(scan->iter);
				return true;
			}
		}
	}

	if (scan->iter)
		return true;

	elog(DEBUG3, "Worker %d iterate_internal_page complete", scan->workerNumber);
	return false;
}

static bool
load_next_disk_leaf_page(BTreeSeqScan *scan)
{
	FileExtent	extent;
	bool		success;
	BTreePageHeader *header;
	BTreeSeqScanDiskDownlink downlink;

	if (scan->downlinkIndex >= scan->downlinksCount)
		return false;

	downlink = scan->diskDownlinks[scan->downlinkIndex];
	success = read_page_from_disk(scan->desc,
								  scan->leafImg,
								  downlink.downlink,
								  &extent);
	header = (BTreePageHeader *) scan->leafImg;
	if (header->csn >= downlink.csn)
		read_page_from_undo(scan->desc, scan->leafImg, header->undoLocation,
							downlink.csn, NULL, BTreeKeyNone, NULL);

	STOPEVENT(STOPEVENT_SCAN_DISK_PAGE,
			  btree_page_stopevent_params(scan->desc,
										  scan->leafImg));

	if (!success)
		elog(ERROR, "can not read leaf page from disk");

	BTREE_PAGE_LOCATOR_FIRST(scan->leafImg, &scan->leafLoc);
	scan->downlinkIndex++;
	scan->hint.blkno = OInvalidInMemoryBlkno;
	scan->hint.pageChangeCount = InvalidOPageChangeCount;
	O_TUPLE_SET_NULL(scan->nextKey.tuple);
	load_first_historical_page(scan);
	return true;
}

static inline
bool single_leaf_page_rel(BTreeSeqScan *scan)
{
	if(scan->poscan)
		return (scan->poscan->flags & O_PARALLEL_IS_SINGLE_LEAF_PAGE) != 0;
	else
		return scan->isSingleLeafPage;
}

static BTreeSeqScan *
make_btree_seq_scan_internal(BTreeDescr *desc, CommitSeqNo csn,
							 BTreeSeqScanCallbacks *cb, void *arg,
							 BlockSampler sampler, ParallelOScanDesc poscan)
{
	BTreeSeqScan *scan = (BTreeSeqScan *) MemoryContextAlloc(btree_seqscan_context,
															 sizeof(BTreeSeqScan));
	uint32		checkpointNumberBefore,
				checkpointNumberAfter;
	bool		checkpointConcurrent;
	BTreeMetaPage *metaPageBlkno = BTREE_GET_META(desc);

	if(poscan)
	{
		SpinLockAcquire(&poscan->workerStart);
		for (scan->workerNumber = 0; poscan->worker_active[scan->workerNumber] == true; scan->workerNumber++) {}

		poscan->worker_active[scan->workerNumber] = true;

		/* leader */
		if (scan->workerNumber == 0)
		{
			Assert(!(poscan->flags & O_PARALLEL_LEADER_STARTED));
			poscan->flags |= O_PARALLEL_LEADER_STARTED;
			scan->isLeader = true;
		}
		SpinLockRelease(&poscan->workerStart);

		elog(DEBUG3, "make_btree_seq_scan_internal. %s %d started", poscan ? "Parallel worker" : "Worker", scan->workerNumber);
	}
	else
	{
		scan->workerNumber = -1;
		scan->isLeader = true;
	}

	scan->poscan = poscan;
	scan->desc = desc;
	scan->snapshotCsn = csn;
	scan->status = BTreeSeqScanInMemory;
	scan->allocatedDownlinks = 16;
	scan->downlinksCount = 0;
	scan->downlinkIndex = 0;
	scan->diskDownlinks = (BTreeSeqScanDiskDownlink *) palloc(sizeof(scan->diskDownlinks[0]) * scan->allocatedDownlinks);
	scan->mctx = CurrentMemoryContext;
	scan->iter = NULL;
	scan->cb = cb;
	scan->arg = arg;
	scan->firstPageIsLoaded = false;
	scan->intStartOffset = 0;
	scan->samplingNumber = 0;
	scan->sampler = sampler;
	if (sampler)
	{
		scan->needSampling = true;
		if (BlockSampler_HasMore(scan->sampler))
			scan->samplingNext = BlockSampler_Next(scan->sampler);
		else
			scan->samplingNext = InvalidBlockNumber;
	}
	else
	{
		scan->needSampling = false;
		scan->samplingNext = InvalidBlockNumber;
	}

	O_TUPLE_SET_NULL(scan->nextKey.tuple);

	START_CRIT_SECTION();
	dlist_push_tail(&listOfScans, &scan->listNode);

	/*
	 * Get the checkpoint number for the scan.  There is race condition with
	 * concurrent switching tree to the next checkpoint.  So, we have to
	 * workaround this with recheck-retry loop,
	 */
	checkpointNumberBefore = get_cur_checkpoint_number(&desc->oids,
													   desc->type,
													   &checkpointConcurrent);
	while (true)
	{
		(void) pg_atomic_fetch_add_u32(&metaPageBlkno->numSeqScans[checkpointNumberBefore % NUM_SEQ_SCANS_ARRAY_SIZE], 1);
		checkpointNumberAfter = get_cur_checkpoint_number(&desc->oids,
														  desc->type,
														  &checkpointConcurrent);
		if (checkpointNumberAfter == checkpointNumberBefore)
		{
			scan->checkpointNumber = checkpointNumberBefore;
			break;
		}
		(void) pg_atomic_fetch_sub_u32(&metaPageBlkno->numSeqScans[checkpointNumberBefore % NUM_SEQ_SCANS_ARRAY_SIZE], 1);
		checkpointNumberBefore = checkpointNumberAfter;
	}
	END_CRIT_SECTION();

	init_page_find_context(&scan->context, desc, csn, BTREE_PAGE_FIND_IMAGE |
						   BTREE_PAGE_FIND_KEEP_LOKEY |
						   BTREE_PAGE_FIND_READ_CSN);
	clear_fixed_key(&scan->prevHikey);
	clear_fixed_key(&scan->keyRangeHigh);
	clear_fixed_key(&scan->keyRangeLow);
	scan->isSingleLeafPage = false;
	if (!iterate_internal_page(scan) && !single_leaf_page_rel(scan))
	{
		switch_to_disk_scan(scan);
		if (!load_next_disk_leaf_page(scan))
			scan->status = BTreeSeqScanFinished;
	}

	return scan;
}

BTreeSeqScan *
make_btree_seq_scan(BTreeDescr *desc, CommitSeqNo csn, void *poscan)
{
	return make_btree_seq_scan_internal(desc, csn, NULL, NULL, NULL, poscan);
}

BTreeSeqScan *
make_btree_seq_scan_cb(BTreeDescr *desc, CommitSeqNo csn,
					   BTreeSeqScanCallbacks *cb, void *arg)
{
	return make_btree_seq_scan_internal(desc, csn, cb, arg, NULL, NULL);
}

BTreeSeqScan *
make_btree_sampling_scan(BTreeDescr *desc, BlockSampler sampler)
{
	return make_btree_seq_scan_internal(desc, COMMITSEQNO_INPROGRESS,
										NULL, NULL, sampler, NULL);
}

static OTuple
btree_seq_scan_get_tuple_from_iterator(BTreeSeqScan *scan,
									   CommitSeqNo *tupleCsn,
									   BTreeLocationHint *hint)
{
	OTuple		result;

	if (!O_TUPLE_IS_NULL(scan->iterEnd))
		result = o_btree_iterator_fetch(scan->iter, tupleCsn,
										&scan->iterEnd, BTreeKeyNonLeafKey,
										false, hint);
	else
		result = o_btree_iterator_fetch(scan->iter, tupleCsn,
										NULL, BTreeKeyNone,
										false, hint);

	if (O_TUPLE_IS_NULL(result))
	{
		btree_iterator_free(scan->iter);
		scan->iter = NULL;
	}
	return result;
}

static bool
adjust_location_with_next_key(BTreeSeqScan *scan,
							  Page p, BTreePageItemLocator *loc)
{
	BTreeDescr *desc = scan->desc;
	BTreePageHeader *header = (BTreePageHeader *) p;
	int			cmp;
	OTuple		key;

	if (!BTREE_PAGE_LOCATOR_IS_VALID(p, loc))
		return false;

	BTREE_PAGE_READ_LEAF_TUPLE(key, p, loc);

	cmp = o_btree_cmp(desc, &key, BTreeKeyLeafTuple,
					  &scan->nextKey.tuple, BTreeKeyNonLeafKey);
	if (cmp == 0)
		return true;
	if (cmp > 0)
		return false;

	while (true)
	{
		if (loc->chunkOffset == (header->chunksCount - 1))
			break;

		key.formatFlags = header->chunkDesc[loc->chunkOffset].hikeyFlags;
		key.data = (Pointer) p + SHORT_GET_LOCATION(header->chunkDesc[loc->chunkOffset].hikeyShortLocation);
		cmp = o_btree_cmp(desc, &key, BTreeKeyNonLeafKey,
						  &scan->nextKey.tuple, BTreeKeyNonLeafKey);
		if (cmp > 0)
			break;
		loc->itemOffset = loc->chunkItemsCount;
		if (!page_locator_next_chunk(p, loc))
		{
			BTREE_PAGE_LOCATOR_SET_INVALID(loc);
			return false;
		}
	}

	while (BTREE_PAGE_LOCATOR_IS_VALID(p, loc))
	{
		BTREE_PAGE_READ_LEAF_TUPLE(key, p, loc);
		cmp = o_btree_cmp(desc,
						  &key, BTreeKeyLeafTuple,
						  &scan->nextKey.tuple, BTreeKeyNonLeafKey);
		if (cmp == 0)
			return true;
		if (cmp > 0)
			break;
		BTREE_PAGE_LOCATOR_NEXT(p, loc);
	}

	return false;
}

static void
apply_next_key(BTreeSeqScan *scan)
{
	BTreeDescr *desc = scan->desc;

	Assert(BTREE_PAGE_LOCATOR_IS_VALID(scan->leafImg, &scan->leafLoc) ||
		   (scan->haveHistImg && BTREE_PAGE_LOCATOR_IS_VALID(scan->histImg, &scan->histLoc)));

	while (true)
	{
		OTuple		key;
		bool		leafResult,
					histResult;

		if (BTREE_PAGE_LOCATOR_IS_VALID(scan->leafImg, &scan->leafLoc))
			BTREE_PAGE_READ_LEAF_TUPLE(key, scan->leafImg, &scan->leafLoc);
		else
			O_TUPLE_SET_NULL(key);

		if (scan->haveHistImg &&
			BTREE_PAGE_LOCATOR_IS_VALID(scan->histImg, &scan->histLoc))
		{
			if (O_TUPLE_IS_NULL(key))
			{
				BTREE_PAGE_READ_LEAF_TUPLE(key, scan->histImg, &scan->histLoc);
			}
			else
			{
				OTuple		histKey;

				BTREE_PAGE_READ_LEAF_TUPLE(histKey, scan->histImg, &scan->histLoc);
				if (o_btree_cmp(desc,
								&key, BTreeKeyLeafTuple,
								&histKey, BTreeKeyNonLeafKey) > 0)
					key = histKey;
			}
		}

		scan->nextKey.tuple = key;
		if (O_TUPLE_IS_NULL(key) ||
			!scan->cb->getNextKey(&scan->nextKey, true, scan->arg))
		{
			BTREE_PAGE_LOCATOR_SET_INVALID(&scan->leafLoc);
			return;
		}

		leafResult = adjust_location_with_next_key(scan,
												   scan->leafImg,
												   &scan->leafLoc);
		if (scan->haveHistImg)
		{
			histResult = adjust_location_with_next_key(scan,
													   scan->histImg,
													   &scan->histLoc);
			if (leafResult || histResult)
				return;
		}
		else if (leafResult)
			return;

		if (!BTREE_PAGE_LOCATOR_IS_VALID(scan->leafImg, &scan->leafLoc) &&
			!(scan->haveHistImg &&
			  BTREE_PAGE_LOCATOR_IS_VALID(scan->histImg, &scan->histLoc)))
			return;
	}
}

static OTuple
btree_seq_scan_getnext_internal(BTreeSeqScan *scan, MemoryContext mctx,
								CommitSeqNo *tupleCsn, BTreeLocationHint *hint)
{
	OTuple		tuple;

	if (scan->iter)
	{
		tuple = btree_seq_scan_get_tuple_from_iterator(scan, tupleCsn, hint);
		if (!O_TUPLE_IS_NULL(tuple))
			return tuple;
	}

	while (true)
	{
		while (scan->haveHistImg)
		{
			OTuple		histTuple;

			while (!BTREE_PAGE_LOCATOR_IS_VALID(scan->histImg, &scan->histLoc))
			{
				if (O_PAGE_IS(scan->histImg, RIGHTMOST))
				{
					scan->haveHistImg = false;
					break;
				}
				if (!O_PAGE_IS(scan->leafImg, RIGHTMOST))
				{
					OTuple		leafHikey,
								histHikey;

					BTREE_PAGE_GET_HIKEY(leafHikey, scan->leafImg);
					BTREE_PAGE_GET_HIKEY(histHikey, scan->histImg);
					if (o_btree_cmp(scan->desc,
									&histHikey, BTreeKeyNonLeafKey,
									&leafHikey, BTreeKeyNonLeafKey) >= 0)
					{
						scan->haveHistImg = false;
						break;
					}
				}
				load_next_historical_page(scan);
			}

			if (!scan->haveHistImg)
				break;

			if (scan->cb && scan->cb->getNextKey)
				apply_next_key(scan);

			if (!BTREE_PAGE_LOCATOR_IS_VALID(scan->histImg, &scan->histLoc))
				continue;

			BTREE_PAGE_READ_LEAF_TUPLE(histTuple, scan->histImg,
									   &scan->histLoc);
			if (!BTREE_PAGE_LOCATOR_IS_VALID(scan->leafImg, &scan->leafLoc))
			{
				OTuple		leafHikey;

				if (!O_PAGE_IS(scan->leafImg, RIGHTMOST))
				{
					BTREE_PAGE_GET_HIKEY(leafHikey, scan->leafImg);
					if (o_btree_cmp(scan->desc,
									&histTuple, BTreeKeyLeafTuple,
									&leafHikey, BTreeKeyNonLeafKey) >= 0)
					{
						scan->haveHistImg = false;
						break;
					}
				}
			}
			else
			{
				BTreeLeafTuphdr *tuphdr;
				OTuple		leafTuple;
				int			cmp;

				BTREE_PAGE_READ_LEAF_ITEM(tuphdr, leafTuple,
										  scan->leafImg, &scan->leafLoc);

				cmp = o_btree_cmp(scan->desc,
								  &histTuple, BTreeKeyLeafTuple,
								  &leafTuple, BTreeKeyLeafTuple);
				if (cmp > 0)
					break;

				if (cmp == 0)
				{
					if (XACT_INFO_OXID_IS_CURRENT(tuphdr->xactInfo))
					{
						BTREE_PAGE_LOCATOR_NEXT(scan->histImg, &scan->histLoc);
						break;
					}
					else
					{
						BTREE_PAGE_LOCATOR_NEXT(scan->leafImg, &scan->leafLoc);
					}
				}
			}

			tuple = o_find_tuple_version(scan->desc,
										 scan->histImg,
										 &scan->histLoc,
										 scan->snapshotCsn,
										 tupleCsn,
										 mctx,
										 NULL,
										 NULL);
			BTREE_PAGE_LOCATOR_NEXT(scan->histImg, &scan->histLoc);
			if (!O_TUPLE_IS_NULL(tuple))
			{
				if (hint)
					*hint = scan->hint;
				return tuple;
			}
		}

		if (scan->cb && scan->cb->getNextKey &&
			BTREE_PAGE_LOCATOR_IS_VALID(scan->leafImg, &scan->leafLoc))
			apply_next_key(scan);

		if (!BTREE_PAGE_LOCATOR_IS_VALID(scan->leafImg, &scan->leafLoc))
		{
			if (scan->status == BTreeSeqScanInMemory)
			{
				elog(DEBUG4, "load_next_in_memory_leaf_page START2");
				if (iterate_internal_page(scan))
				{
					if (scan->iter)
					{
						tuple = btree_seq_scan_get_tuple_from_iterator(scan,
																	   tupleCsn,
																	   hint);
						if (!O_TUPLE_IS_NULL(tuple))
							return tuple;
					}
				}
				else
				{
					switch_to_disk_scan(scan);
				}
			}
			if (scan->status == BTreeSeqScanDisk)
			{
				if (!load_next_disk_leaf_page(scan))
				{
					scan->status = BTreeSeqScanFinished;
					O_TUPLE_SET_NULL(tuple);
					return tuple;
				}
			}
			continue;
		}

		tuple = o_find_tuple_version(scan->desc,
									 scan->leafImg,
									 &scan->leafLoc,
									 scan->snapshotCsn,
									 tupleCsn,
									 mctx,
									 NULL,
									 NULL);
		BTREE_PAGE_LOCATOR_NEXT(scan->leafImg, &scan->leafLoc);
		if (!O_TUPLE_IS_NULL(tuple))
		{
			if (hint)
				*hint = scan->hint;
			return tuple;
		}
	}

	/* keep compiler quiet */
	O_TUPLE_SET_NULL(tuple);
	return tuple;
}

OTuple
btree_seq_scan_getnext(BTreeSeqScan *scan, MemoryContext mctx,
					   CommitSeqNo *tupleCsn, BTreeLocationHint *hint)
{
	OTuple		tuple;

	if (scan->status == BTreeSeqScanInMemory ||
		scan->status == BTreeSeqScanDisk)
	{
		tuple = btree_seq_scan_getnext_internal(scan, mctx, tupleCsn, hint);

		if (!O_TUPLE_IS_NULL(tuple))
			return tuple;
	}
	Assert(scan->status == BTreeSeqScanFinished);

	O_TUPLE_SET_NULL(tuple);
	return tuple;
}

static OTuple
btree_seq_scan_get_tuple_from_iterator_raw(BTreeSeqScan *scan,
										   bool *end,
										   BTreeLocationHint *hint)
{
	OTuple		result;

	if (!O_TUPLE_IS_NULL(scan->iterEnd))
		result = btree_iterate_raw(scan->iter, &scan->iterEnd, BTreeKeyNonLeafKey,
								   false, end, hint);
	else
		result = btree_iterate_raw(scan->iter, NULL, BTreeKeyNone,
								   false, end, hint);

	if (*end)
	{
		btree_iterator_free(scan->iter);
		scan->iter = NULL;
	}
	return result;
}

static OTuple
btree_seq_scan_getnext_raw_internal(BTreeSeqScan *scan, MemoryContext mctx,
									BTreeLocationHint *hint)
{
	BTreeLeafTuphdr *tupHdr;
	OTuple		tuple;

	if (scan->iter)
	{
		bool		end;

		tuple = btree_seq_scan_get_tuple_from_iterator_raw(scan, &end, hint);
		if (!end)
			return tuple;
	}

	while (!BTREE_PAGE_LOCATOR_IS_VALID(scan->leafImg, &scan->leafLoc))
	{
		if (scan->status == BTreeSeqScanInMemory)
		{
			elog(DEBUG3, "load_next_in_memory_leaf_page START3");
			if (iterate_internal_page(scan))
			{
				if (scan->iter)
				{
					bool		end;

					tuple = btree_seq_scan_get_tuple_from_iterator_raw(scan, &end, hint);
					if (!end)
						return tuple;
				}
			}
			else
			{
				switch_to_disk_scan(scan);
			}
		}
		if (scan->status == BTreeSeqScanDisk)
		{
			if (!load_next_disk_leaf_page(scan))
			{
				scan->status = BTreeSeqScanFinished;
				O_TUPLE_SET_NULL(tuple);
				return tuple;
			}
		}
	}

	BTREE_PAGE_READ_LEAF_ITEM(tupHdr, tuple, scan->leafImg, &scan->leafLoc);
	BTREE_PAGE_LOCATOR_NEXT(scan->leafImg, &scan->leafLoc);

	if (!tupHdr->deleted)
	{
		if (hint)
			*hint = scan->hint;

		return tuple;
	}
	else
	{
		O_TUPLE_SET_NULL(tuple);
		return tuple;
	}
}

OTuple
btree_seq_scan_getnext_raw(BTreeSeqScan *scan, MemoryContext mctx,
						   bool *end, BTreeLocationHint *hint)
{
	OTuple		tuple;

	if (scan->status == BTreeSeqScanInMemory ||
		scan->status == BTreeSeqScanDisk)
	{
		tuple = btree_seq_scan_getnext_raw_internal(scan, mctx, hint);
		if (scan->status == BTreeSeqScanInMemory ||
			scan->status == BTreeSeqScanDisk)
		{
			*end = false;
			return tuple;
		}
	}
	Assert(scan->status == BTreeSeqScanFinished);

	O_TUPLE_SET_NULL(tuple);
	*end = true;
	return tuple;
}

void
free_btree_seq_scan(BTreeSeqScan *scan)
{
	BTreeMetaPage *metaPageBlkno = BTREE_GET_META(scan->desc);

	START_CRIT_SECTION();
	dlist_delete(&scan->listNode);
	(void) pg_atomic_fetch_sub_u32(&metaPageBlkno->numSeqScans[scan->checkpointNumber % NUM_SEQ_SCANS_ARRAY_SIZE], 1);
	END_CRIT_SECTION();

	pfree(scan->diskDownlinks);
	pfree(scan);
}

/*
 * Error cleanup for sequential scans.  No scans survives the error, but they
 * are't cleaned up individually.  Thus, we have to walk trough all the scans
 * and revert changes made to the metaPageBlkno->numSeqScans.
 */
void
seq_scans_cleanup(void)
{
	START_CRIT_SECTION();
	while (!dlist_is_empty(&listOfScans))
	{
		BTreeSeqScan *scan = dlist_head_element(BTreeSeqScan, listNode, &listOfScans);
		BTreeMetaPage *metaPageBlkno;

		/* TODO cleanup after parallel scan */
		if (!scan->poscan)
		{
			metaPageBlkno = BTREE_GET_META(scan->desc);

			(void) pg_atomic_fetch_sub_u32(&metaPageBlkno->numSeqScans[scan->checkpointNumber % NUM_SEQ_SCANS_ARRAY_SIZE], 1);
		}
		dlist_delete(&scan->listNode);
		pfree(scan);
	}
	dlist_init(&listOfScans);
	END_CRIT_SECTION();
}
