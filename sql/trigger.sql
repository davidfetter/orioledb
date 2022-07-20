
-- ERROR:  tuple to be updated was already modified by an operation triggered by the current command
-- HINT:  Consider using an AFTER trigger instead of a BEFORE trigger to propagate changes to other rows.


CREATE EXTENSION IF NOT EXISTS orioledb;

CREATE TEMP TABLE o_test_1(
    val_1 int PRIMARY KEY,
    val_2 int REFERENCES o_test_1,
    val_3 text,
    val_4 int NOT NULL DEFAULT 0
)USING orioledb;


CREATE FUNCTION func_1()
  RETURNS TRIGGER AS
$$
BEGIN
  IF old.val_2 IS NOT NULL THEN
    UPDATE o_test_1 SET val_4 = val_4 - 1
      where val_1 = old.val_2;
  END IF;
  RETURN OLD;
end;
$$LANGUAGE plpgsql;

CREATE TRIGGER trig_1 BEFORE DELETE ON o_test_1
  FOR EACH ROW EXECUTE PROCEDURE func_1();

INSERT INTO o_test_1 VALUES (1, NULL, 'root');
INSERT INTO o_test_1 VALUES (2, 1, 'root child A');

UPDATE o_test_1 SET val_3 = 'root!' WHERE val_1 = 1;

DELETE FROM o_test_1;