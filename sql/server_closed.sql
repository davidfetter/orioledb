

CREATE EXTENSION IF NOT EXISTS orioledb;

CREATE TABLE o_test_1 (
	val_1	int PRIMARY KEY,
	val_2	int
)USING orioledb;

CREATE TABLE o_test_2 (
	val_3		int PRIMARY KEY,
	val_4		int REFERENCES o_test_1 DEFERRABLE
)USING orioledb;


BEGIN;

SET CONSTRAINTS ALL DEFERRED;

INSERT INTO o_test_2 VALUES (10, 15);

COMMIT;

----------------------------------


CREATE EXTENSION IF NOT EXISTS orioledb;

CREATE TABLE o_test_1 (val_1 int, val_2 text, PRIMARY KEY (val_1, val_2)) PARTITION BY RANGE (val_1);

CREATE TABLE o_test_2 (val_3 int, LIKE o_test_1)USING orioledb;

ALTER TABLE o_test_2 DROP COLUMN val_3;

ALTER TABLE o_test_1 ATTACH PARTITION o_test_2 for VALUES FROM (10) TO (100);

SELECT tableoid::regclass, * FROM o_test_1 ORDER BY val_1;


----------------------------------


CREATE EXTENSION IF NOT EXISTS orioledb;

CREATE TYPE test_type AS (val_1 float8, val_2 float8);

CREATE DOMAIN test_domain AS test_type;

CREATE TABLE o_test_1 (d1 test_domain UNIQUE);--USING orioledb;

INSERT INTO o_test_1 VALUES (ROW(1,2)::test_domain);

INSERT INTO o_test_1 VALUES (ROW(3,4)::test_type);


----------------------------------

CREATE EXTENSION IF NOT EXISTS orioledb;

CREATE TABLE o_test_1 (
    val_1 text, 
    val_2 text GENERATED ALWAYS AS (val_1 || '+' || val_1) STORED
)USING orioledb;

INSERT INTO o_test_1 (val_1) VALUES ('a'), ('b'), ('c'), (NULL);

SELECT * FROM o_test_1;

----------------------------------

CREATE EXTENSION IF NOT EXISTS orioledb;

CREATE TABLE o_test_1(
  val_1 text COMPRESSION pglz
)USING orioledb;

INSERT INTO o_test_1 
  VALUES(repeat('a', 1));

CREATE OR REPLACE FUNCTION func_1() RETURNS TEXT AS
$$
  SELECT array_agg(md5(g::text))::text 
  FROM generate_series(1, 256) g
$$ LANGUAGE SQL;

CREATE TABLE o_test_2 (val_1 text COMPRESSION pglz)USING orioledb;

INSERT INTO o_test_2 SELECT func_1() || repeat('b', 2);

SELECT pg_column_compression(val_1) FROM o_test_2;


----------------------------------

CREATE EXTENSION IF NOT EXISTS orioledb;

CREATE TABLE o_test_1 (
  val_1 int DEFAULT 1,
  val_2 text DEFAULT 'a', 
  val_3 float8 DEFAULT 1.1
)USING orioledb;

INSERT INTO o_test_1 VALUES (2, null, 2.0);

SELECT * FROM o_test_1;


----------------------------------

CREATE EXTENSION IF NOT EXISTS orioledb;

CREATE TABLE o_test_1 (
    val_1 int,
    val_2 text COLLATE "C" NOT NULL
)USING orioledb;


CREATE INDEX ind_1 ON o_test_1 (val_2);
CREATE INDEX ind_2 ON o_test_1 (val_2 COLLATE "POSIX");
CREATE INDEX ind_3 ON o_test_1 ((val_2 COLLATE "POSIX"));


----------------------------------

CREATE TYPE test_type_1
  AS (val_1 int, val_2 int);

CREATE TYPE test_type_2
  AS RANGE (subtype = test_type_1);

SELECT *, row_to_json(upper(t)) AS u FROM
  (VALUES (test_type_2(ROW(1,2), ROW(3,4))),
          (test_type_2(ROW(5,6), ROW(7,8)))) v(t);
         

----------------------------------


CREATE TYPE o_test_1 AS (val_1 int, val_2 int);

SELECT ROW(1, 2)::o_test_1 < ROW(1, 3)::o_test_1;


----------------------------------


CREATE EXTENSION IF NOT EXISTS orioledb;

CREATE TABLE o_test_1 (val_1 int, val_2 text);--USING orioledb;

ALTER TABLE o_test_1 ALTER COLUMN val_1 ADD GENERATED ALWAYS AS IDENTITY; 


----------------------------------
         
CREATE USER regress_rls_alice NOLOGIN;
CREATE USER regress_rls_carol NOLOGIN;

CREATE SCHEMA test_schema;
GRANT ALL ON SCHEMA test_schema to public;
SET search_path = test_schema;

SET SESSION AUTHORIZATION regress_rls_alice;
CREATE TABLE o_test_1 (a int, b int, c int);
CREATE POLICY p1 ON o_test_1 USING (o_test_1 >= ROW(1,1,1));

ALTER TABLE o_test_1 ENABLE ROW LEVEL SECURITY;
ALTER TABLE o_test_1 FORCE ROW LEVEL SECURITY;

INSERT INTO o_test_1 SELECT 10, 20, 30;

----------------------------------

CREATE EXTENSION IF NOT EXISTS orioledb;

create table o_test_1 (
	val_1 int,
	val_2 int,
	val_3 text
)partition by list (val_2) ;

create table o_test_2 (val_3 text, val_2 int, val_1 int)USING orioledb;
create table o_test_3 (val_1 int, val_3 text, val_2 int)USING orioledb;

alter table o_test_1 attach partition o_test_2 for values in(1);
alter table o_test_1 attach partition o_test_3 for values in(2);

truncate o_test_1;

create index on o_test_1 (val_2);

select * from o_test_1 where val_2 = 1;

----------------------------------

CREATE EXTENSION IF NOT EXISTS orioledb;

CREATE SCHEMA test_schema;
CREATE USER test_user;

ALTER DEFAULT PRIVILEGES FOR ROLE test_user
	  REVOKE INSERT ON TABLES FROM test_user;

PREPARE data_sel AS SELECT generate_series(1,3);

EXPLAIN (ANALYZE, COSTS OFF, SUMMARY OFF, TIMING OFF)
  CREATE TABLE test_schema.tbl_nodata4 (a) USING orioledb 
  AS
  EXECUTE data_sel WITH NO DATA;

SET SESSION AUTHORIZATION test_user;
RESET SESSION AUTHORIZATION;

DROP SCHEMA test_schema CASCADE;

----------------------------------

create table o_test_1 (a int, b int)
  partition by list ((row(a, b)::o_test_1));

create table o_test_2
  partition of o_test_1 for values in ('(1,2)'::o_test_1);

create table o_test_3
  partition of o_test_1 for values in ('(2,4)'::o_test_1);
  
----------------------------------

CREATE EXTENSION IF NOT EXISTS orioledb;

CREATE TABLE o_test_1 (
  a int, 
  b int,
  c text,
  d int
) PARTITION BY RANGE (a, b);

CREATE TABLE o_test_2 PARTITION of o_test_1
  FOR VALUES FROM (1, 40) TO (1, 50) PARTITION BY RANGE (c);
  
CREATE TABLE o_test_3 PARTITION OF o_test_2
  FOR VALUES FROM ('a') TO ('c') PARTITION BY LIST (c);

CREATE TABLE o_test_4 (
  d int, 
  b int, 
  c text, 
  a int
)USING orioledb;

ALTER TABLE o_test_3 
  ATTACH PARTITION o_test_4 FOR VALUES IN ('b');

SELECT tableoid::regclass, * FROM o_test_1 ORDER BY a, b, c, d;

ALTER TABLE o_test_1 DROP d;

ALTER TABLE o_test_1 ADD d int;

INSERT INTO o_test_1 values (1, 45, 'b', 1);  
INSERT INTO o_test_1 values (1, 45, 'c', 1); 
INSERT INTO o_test_1 values (1, 45, 'f', 1);  

SELECT tableoid::regclass, * FROM o_test_1 ORDER BY a, b, c, d;

----------------------------------

SET SESSION CHARACTERISTICS AS TRANSACTION READ WRITE;

CREATE EXTENSION IF NOT EXISTS orioledb;

CREATE TABLE o_test_1(
  val_1 int
)USING orioledb;

CREATE TABLE o_test_2(
  val_2 int
)USING orioledb;


BEGIN;
	INSERT INTO o_test_1 VALUES (1);
	SAVEPOINT two;
		INSERT into o_test_2 VALUES (1);
	RELEASE two;
	SAVEPOINT three;
		SAVEPOINT four;
			INSERT INTO o_test_1 VALUES (2);
		RELEASE SAVEPOINT four;
	ROLLBACK TO SAVEPOINT three;

----------------------------------


CREATE EXTENSION IF NOT EXISTS orioledb;

CREATE FUNCTION func_1(text) RETURNS bool AS 
$$ 
  BEGIN 
    RETURN true; 
  END; 
$$ LANGUAGE plpgsql immutable;

CREATE OR REPLACE FUNCTION func_2() 
RETURNS trigger AS 
$$
  BEGIN
    RETURN NULL;
  END;
  $$ LANGUAGE plpgsql;

CREATE TABLE o_test_1(
  a int, 
  b text)
PARTITION BY RANGE (b);

CREATE TABLE o_test_2(
  a int, 
  b text)
PARTITION BY RANGE (b);

ALTER TABLE o_test_1 ATTACH PARTITION o_test_2
  FOR VALUES FROM ('a') TO ('z');

CREATE TABLE o_test_3(
  a int, 
  b text
)USING orioledb;

ALTER TABLE o_test_2 ATTACH PARTITION o_test_3
  FOR VALUES FROM ('a') TO ('b');

CREATE CONSTRAINT TRIGGER trig_1 AFTER INSERT ON o_test_2
  DEFERRABLE INITIALLY DEFERRED
  FOR EACH ROW WHEN (func_1(new.b) AND new.a % 2 = 1)
  EXECUTE PROCEDURE func_2();

BEGIN;
INSERT INTO o_test_2 VALUES (1, 'aa');
INSERT INTO o_test_3 VALUES (2, 'aa');
INSERT INTO o_test_1 VALUES (3, 'aa');
COMMIT;

----------------------------------

CREATE EXTENSION IF NOT EXISTS orioledb;

CREATE FUNCTION func_1(int) RETURNS int AS $$
DECLARE TOTAL int;
BEGIN
	CREATE TEMP TABLE o_test_1(val_1 int)USING orioledb;
	INSERT INTO o_test_1 VALUES($1);
	INSERT INTO o_test_1 VALUES(11);
	INSERT INTO o_test_1 VALUES(12);
	INSERT INTO o_test_1 VALUES(13);
	SELECT sum(val_1) INTO total FROM o_test_1;
	DROP TABLE o_test_1;
	RETURN total;
end
$$ language plpgsql;

SELECT func_1(1);
SELECT func_1(2);
SELECT func_1(3);
