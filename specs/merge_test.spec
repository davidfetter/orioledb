setup
{
	CREATE EXTENSION IF NOT EXISTS orioledb;

	CREATE TABLE o_test_1 (
		val_1 int PRIMARY KEY, 
		val_2 int, 
		val_3 int
	)USING orioledb;

	INSERT INTO o_test_1 
		VALUES (1, 2, 3);
		
}

teardown
{
	DROP TABLE o_test_1;
}

session "s1"

step "begin_1" { BEGIN;}

step "delete_1" { DELETE FROM o_test_1
										WHERE val_1 = 1; }

step "merge_delete_1" { MERGE INTO o_test_1 t 
													USING (SELECT 1 AS val_1) s 
													ON s.val_1 = t.val_1 
												WHEN MATCHED THEN DELETE; }

step "merge_update_1" {	MERGE INTO o_test_1 t
													USING (SELECT 1 AS val_1) s
													ON s.val_1 = t.val_1
												WHEN MATCHED AND val_3 = 3 THEN
													UPDATE SET val_3 = 33; }

step "commit_1" { COMMIT; }

session "s2"

step "begin_2" { BEGIN; }

step "delete_2" { DELETE FROM o_test_1
									WHERE val_1 = 1; }

step "update_2" { UPDATE o_test_1 SET val_2 = val_2 + 10; }

step "merge_2" { MERGE INTO o_test_1 t 
										USING (SELECT 1 AS val_1) s 
										ON s.val_1 = t.val_1
									WHEN MATCHED THEN 
										UPDATE SET val_1 = t.val_1 + 1; }
										
step "commit_2" { COMMIT; }


permutation "begin_1" "begin_2" 
"delete_1" "update_2" 
"commit_1" "commit_2"

permutation "begin_1" "begin_2" 
"merge_delete_1" "update_2" 
"commit_1" "commit_2"

permutation "begin_1" "begin_2"
"delete_1" "merge_2" 
"commit_1" "commit_2"

permutation "begin_1" "begin_2"
"merge_delete_1" "merge_2" 
"commit_1" "commit_2"

permutation "begin_1" "begin_2" 
"update_2" "merge_update_1" 
"commit_2" "commit_1"		