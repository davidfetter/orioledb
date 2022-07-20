
--ERROR:  failed to fetch tuple1 for AFTER trigger

CREATE EXTENSION IF NOT EXISTS orioledb;

CREATE TABLE o_test_1(
  val_1 int PRIMARY KEY,
  val_2 text
)USING orioledb;

CREATE FUNCTION func_1()
  RETURNS TRIGGER AS 
$$
  BEGIN
    RETURN NULL;
  END;
$$LANGUAGE plpgsql;

CREATE TRIGGER trig_1
  AFTER UPDATE ON o_test_1
  REFERENCING OLD TABLE AS a NEW TABLE AS i
  FOR EACH STATEMENT EXECUTE FUNCTION func_1();

UPDATE o_test_1 SET val_1 = val_1;
