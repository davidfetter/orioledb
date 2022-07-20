

-- expected

-- SELECT count(*) FROM o_test_1;
--  count 
-- -------
--      3
-- (1 row)

-- DELETE FROM o_test_1 WHERE val_1 != 0;
-- SELECT * FROM o_test_1;
--  val_1 
-- -------
--      0
-- (1 row)

-----------------------------

-- results

-- SELECT count(*) FROM o_test_1;
--  count 
-- -------
--      3
-- (1 row)

-- DELETE FROM o_test_1 WHERE val_1 != 0;
-- SELECT * FROM o_test_1;
--  val_1 
-- -------
--      1
--      0
-- (2 rows)



CREATE EXTENSION IF NOT EXISTS orioledb;

CREATE TABLE o_test_1 (val_1 INT)USING orioledb;
INSERT INTO o_test_1 VALUES (1);
INSERT INTO o_test_1 SELECT * FROM o_test_1;
INSERT INTO o_test_1 VALUES (0);
SELECT count(*) FROM o_test_1;
DELETE FROM o_test_1 WHERE val_1 != 0;
SELECT * FROM o_test_1;