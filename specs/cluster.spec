#ERROR Not implemented: orioledb_relation_copy_for_cluster

setup
{
  	CREATE EXTENSION IF NOT EXISTS orioledb;
	CREATE ROLE o_test_role_1;
	CREATE TABLE o_test_1 (val_1 int) USING orioledb;
	CREATE INDEX ind_test_1 ON o_test_1(val_1);
	ALTER TABLE o_test_1 OWNER TO o_test_role_1;
}

teardown
{
	DROP TABLE o_test_1;
	DROP ROLE o_test_role_1;
}

session s1
step "begin_1"          { BEGIN; }
step "lock_1"           { LOCK o_test_1 IN SHARE UPDATE EXCLUSIVE MODE; }
step "commit_1"         { COMMIT; }

session s2
step "auth_2"           { SET ROLE o_test_role_1; }
step "cluster_2"        { CLUSTER o_test_1 USING ind_test_1; }
step "reset_2"          { RESET ROLE; }

permutation "begin_1" "lock_1" "auth_2" "cluster_2" "commit_1" "reset_2"
permutation "begin_1" "auth_2" "lock_1" "cluster_2" "commit_1" "reset_2"
