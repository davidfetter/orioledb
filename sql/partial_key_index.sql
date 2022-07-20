
-- ERROR:  could not create unique index "partial_key_index"
-- DETAIL:  Duplicate keys exist.

CREATE EXTENSION IF NOT EXISTS orioledb;

CREATE TABLE o_test_1(
    val_1 int4, 
    val_2 text
)USING orioledb;

CREATE UNIQUE INDEX ind_1 
    ON o_test_1(val_1, lower(val_2));

INSERT INTO o_test_1 VALUES (25, 'a') 
    ON CONFLICT (lower(val_2), val_1) 
    DO UPDATE SET val_2 = excluded.val_2;

DROP INDEX ind_1;

CREATE UNIQUE INDEX ind_2 
    ON o_test_1(val_2);

INSERT INTO o_test_1 values (25, 'b') 
    ON CONFLICT (val_2) 
    DO UPDATE SET val_2 = excluded.val_2;

DROP INDEX ind_2;

CREATE UNIQUE INDEX partial_key_index 
    ON o_test_1(val_1) 
    WHERE val_2 LIKE '%c';