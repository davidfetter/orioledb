
--ERROR:  Not implemented: orioledb_relation_copy_for_cluster

CREATE EXTENSION IF NOT EXISTS orioledb;

CREATE TEMP TABLE o_test_1(
    val_1 int
)USING orioledb;

CREATE INDEX ind_1 ON o_test_1 (val_1);

CLUSTER o_test_1 USING ind_1;
