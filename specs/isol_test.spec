#ERROR:  could not serialize access due to concurrent update

setup
{
    CREATE EXTENSION IF NOT EXISTS orioledb;

    DROP TABLE IF EXISTS o_test_1;

    CREATE TABLE o_test_1(
      val_1 int, 
      val_2 int
    )USING orioledb;
    
    INSERT INTO o_test_1 
      VALUES (1, 1);
}

teardown
{
    DROP TABLE o_test_1;
}

session s1

step "begin_1" { BEGIN; }
step "select_lock_1" { SELECT pg_advisory_lock(380170116); }
step "update_1" { UPDATE o_test_1 SET val_2 = 2 WHERE val_1 = 1; }
step "select_1" { SELECT * FROM o_test_1; }
step "select_unlock_1" { SELECT pg_advisory_unlock(380170116); }
step "commit_1" { COMMIT; }

teardown { SELECT pg_advisory_unlock_all(); }

session s2

step "begin_repeatable_read_2" { BEGIN ISOLATION LEVEL REPEATABLE READ; }
step "begin_serializavle_2" { BEGIN ISOLATION LEVEL SERIALIZABLE; }
step "select_2" { SELECT * FROM o_test_1 WHERE pg_advisory_lock(380170116) IS NOT NULL FOR KEY SHARE; }
step "commit_2" { COMMIT; }

teardown { SELECT pg_advisory_unlock_all(); }


permutation "begin_1" "begin_repeatable_read_2" "select_lock_1" "select_2" "update_1" "commit_1" "select_unlock_1" "commit_2"
permutation "begin_1" "begin_repeatable_read_2" "select_lock_1" "update_1" "select_2" "commit_1" "select_unlock_1" "commit_2"
permutation "begin_1" "begin_repeatable_read_2" "select_lock_1" "select_2" "update_1" "commit_1" "select_1" "select_unlock_1" "commit_2"
permutation "begin_1" "begin_repeatable_read_2" "select_lock_1" "update_1" "select_2" "commit_1" "select_1" "select_unlock_1" "commit_2"
permutation "begin_1" "begin_serializavle_2" "select_lock_1" "select_2" "update_1" "commit_1" "select_unlock_1" "commit_2"
permutation "begin_1" "begin_serializavle_2" "select_lock_1" "update_1" "select_2" "commit_1" "select_unlock_1" "commit_2"
permutation "begin_1" "begin_serializavle_2" "select_lock_1" "select_2" "update_1" "commit_1" "select_1" "select_unlock_1" "commit_2"
permutation "begin_1" "begin_serializavle_2" "select_lock_1" "update_1" "select_2" "commit_1" "select_1" "select_unlock_1" "commit_2"
