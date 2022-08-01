CREATE EXTENSION IF NOT EXISTS orioledb;

CREATE TABLE o_test_1 (
    val_1 name,
    val_2 name,
    val_3 int
)USING orioledb;

CREATE TABLE o_test_2 (
    val_1 name,
    val_2 name,
    val_3 int
)USING orioledb;

CREATE POLICY pol_1 ON o_test_2;

ALTER TABLE o_test_1 ENABLE ROW LEVEL SECURITY;

CREATE ROLE rol_1;
CREATE ROLE rol_2;

GRANT SELECT,INSERT ON o_test_1 TO rol_1;
GRANT SELECT,INSERT ON o_test_2 TO rol_1;
GRANT SELECT,INSERT ON o_test_1 TO rol_2;
GRANT SELECT,INSERT ON o_test_2 TO rol_2;

SET ROLE rol_1;

INSERT INTO o_test_1 VALUES ('rol_1','rol_2',10);