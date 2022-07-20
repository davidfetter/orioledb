
--ERROR:  Not implemented: orioledb_tuple_tid_valid

CREATE EXTENSION IF NOT EXISTS orioledb;

CREATE TEMP TABLE forc_test USING orioledb
AS
  SELECT n AS i, n AS j FROM generate_series(1,10) n;

CREATE OR REPLACE FUNCTION func_1() RETURNS void AS $$
DECLARE
  c CURSOR for SELECT * FROM forc_test;
BEGIN
  FOR r IN c LOOP
    RAISE NOTICE '%, %', r.i, r.j;
    UPDATE forc_test SET i = i * 100, j = r.j * 2 WHERE CURRENT OF c;
  END LOOP;
END;
$$ LANGUAGE plpgsql;

SELECT func_1();

-------------------------

--ERROR:  Not implemented: orioledb_tuple_tid_valid

CREATE EXTENSION IF NOT EXISTS orioledb;

CREATE TEMP TABLE o_test_1(
  val_1 int, 
  val_2 text
)USING orioledb;

INSERT INTO o_test_1 VALUES (1, 'one'), (2, 'two'), (3, 'three');

BEGIN;

DECLARE test_1 CURSOR FOR SELECT * FROM o_test_1;
FETCH 2 FROM test_1;
DELETE FROM o_test_1 WHERE CURRENT OF test_1;

COMMIT;

BEGIN;
DECLARE test_1 CURSOR FOR SELECT * FROM o_test_1;
FETCH test_1;
UPDATE o_test_1 SET val_1 = val_1 + 10 WHERE CURRENT OF test_1;

ROLLBACK;

CREATE TEMP TABLE o_test_2 () inherits (o_test_1);
INSERT INTO o_test_2 values(100, 'hundred');

BEGIN;
DECLARE test_1 CURSOR FOR SELECT * FROM o_test_1 FOR UPDATE;
FETCH 1 FROM test_1;
UPDATE o_test_1 SET val_1 = val_1 + 10 WHERE CURRENT OF test_1;
FETCH 1 FROM test_1;
UPDATE o_test_1 SET val_1 = val_1 + 10 WHERE CURRENT OF test_1;
FETCH 1 FROM test_1;
UPDATE o_test_1 SET val_1 = val_1 + 10 WHERE CURRENT OF test_1;
FETCH 1 FROM test_1;
COMMIT;

BEGIN;
DECLARE test_1 CURSOR FOR SELECT * FROM o_test_1 a, o_test_1 b 
  WHERE a.val_1 = b.val_1 + 5 FOR UPDATE;
FETCH 1 FROM test_1;
ROLLBACK;

BEGIN;
DECLARE test_1 CURSOR FOR SELECT * FROM o_test_1 a, o_test_1 b 
  WHERE a.val_1 = b.val_1 + 5 FOR SHARE OF a;
FETCH 1 FROM test_1;
ROLLBACK;

-------------------------

--ERROR:  Not implemented: orioledb_tuple_tid_valid

CREATE EXTENSION IF NOT EXISTS orioledb;

CREATE TABLE o_test_1 (val_1 int)USING orioledb;

CREATE VIEW test_view AS SELECT ctid, val_1 FROM o_test_1;

SELECT currtid2('test_view'::text, '(0,1)'::tid);

INSERT INTO o_test_1 VALUES (1);

SELECT currtid2('test_view'::text, '(0,1)'::tid);

