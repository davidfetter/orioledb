
--ERROR:  domain test_domain_1 does not allow null values

CREATE EXTENSION IF NOT EXISTS orioledb;

CREATE DOMAIN test_domain_1 AS TEXT NOT NULL;

CREATE table o_test_1 (
    val_1 int, 
    val_2 int
)USING orioledb;

INSERT INTO o_test_1 
    VALUES (1, 2);

alter table o_test_1 
    ADD COLUMN val_3 test_domain_1;

CREATE DOMAIN test_domain2 
    AS text CHECK (VALUE <> 'foo') DEFAULT 'foo';

alter table o_test_1 
    ADD COLUMN val_4 test_domain2;