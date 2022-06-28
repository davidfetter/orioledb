CREATE EXTENSION IF NOT EXISTS orioledb;

CREATE TYPE o_test_type_1 AS ENUM (
    'a', 'b', 'c', 'd', 'e', 'f');

CREATE TYPE o_test_type_2 AS (
    val_1 integer,
    val_2 o_test_type_1);

CREATE TABLE o_test_1 USING orioledb
AS SELECT ((random()*10), 
           (ENUM_RANGE(NULL::o_test_type_1))[val])::o_test_type_2 
    AS val_1
    FROM generate_series(1,10) val;

CREATE FUNCTION func_test_1(a o_test_type_2) RETURNS numeric
AS $$
    SELECT a.val_1;
$$ LANGUAGE SQL STRICT IMMUTABLE;

CREATE FUNCTION func_test_2(a o_test_type_2, b o_test_type_2)
RETURNS integer 
AS $$
    SELECT (func_test_1(a) - func_test_1(b));
$$ LANGUAGE SQL STRICT IMMUTABLE;

CREATE FUNCTION func_test_3(a o_test_type_2, b o_test_type_2)
RETURNS boolean
AS $$
    BEGIN
    RETURN func_test_2(a,b) < 0;
    END;
$$ LANGUAGE plpgsql IMMUTABLE STRICT;

CREATE OPERATOR #<# (
    LEFTARG = o_test_type_2,
    RIGHTARG = o_test_type_2,
    FUNCTION = func_test_3);

CREATE OPERATOR CLASS test_class
    FOR TYPE o_test_type_2
    USING btree AS
    OPERATOR 1 #<#,
    FUNCTION 1 func_test_2(o_test_type_2,o_test_type_2);

CREATE INDEX val_ind ON o_test_1(val_1 test_class);