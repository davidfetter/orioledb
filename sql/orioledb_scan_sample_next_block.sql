
--ERROR:  Not implemented: orioledb_scan_sample_next_block

CREATE EXTENSION IF NOT EXISTS orioledb;

CREATE TABLE o_test_1(
  val_1 int, 
  name text
)USING orioledb;

SELECT t.val_1 FROM o_test_1 AS t TABLESAMPLE SYSTEM (50) REPEATABLE (0);

SELECT count(*) FROM o_test_1 TABLESAMPLE SYSTEM (100);

