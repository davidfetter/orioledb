
--ERROR:  virtual tuple table slot does not have system attributes

CREATE EXTENSION IF NOT EXISTS orioledb;

CREATE TABLE o_test_1 (
    val_1 int PRIMARY KEY,
    val_2 text
)USING orioledb;

INSERT INTO o_test_1 VALUES (2, 'a') ON CONFLICT(val_1)
  DO UPDATE SET (val_2, val_1) = (SELECT val_2 || ', c', val_1 FROM o_test_1 i 
  WHERE i.val_1 = excluded.val_1)
  RETURNING tableoid::regclass, xmin = pg_current_xact_id()::xid 
  AS xmin_correct, xmax = 0 AS xmax_correct;

INSERT INTO o_test_1 VALUES (2, 'b') ON CONFLICT(val_1)
  DO UPDATE SET (val_2, val_1) = (SELECT val_2 || ', c', val_1 FROM o_test_1 i 
  WHERE i.val_1 = excluded.val_1)
  RETURNING tableoid::regclass, xmin = pg_current_xact_id()::xid 
  AS xmin_correct, xmax = pg_current_xact_id()::xid 
  AS xmax_correct;

------------------------------------------

--ERROR:  virtual tuple table slot does not have system attributes

CREATE EXTENSION IF NOT EXISTS orioledb;

CREATE TEMP TABLE o_test_1 (val_1 int)USING orioledb;

INSERT INTO o_test_1 VALUES (1);

SELECT ctid,cmin,* FROM o_test_1;

--------------------------------------------------

CREATE EXTENSION IF NOT EXISTS orioledb;

CREATE TABLE o_test(val_1 serial, val_2 timestamptz default now())USING orioledb;

INSERT INTO o_test DEFAULT VALUES;

SELECT val_1,
       pg_xact_commit_timestamp(xmin) >= val_2
FROM o_test
ORDER BY val_1;

DROP TABLE o_test;

