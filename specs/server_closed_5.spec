#without orioledb - ERROR:  tuple to be updated was 
#already modified by an operation triggered by the 
#current command
#with orioledb - server closed

setup
{
    CREATE EXTENSION IF NOT EXISTS orioledb;

    CREATE TABLE o_test_1(
      val_1 text PRIMARY KEY, 
      val_2 int not null
    )USING orioledb;

    INSERT INTO o_test_1 
      VALUES ('a', 10), ('b', 10);

    CREATE FUNCTION func_1(int) 
    RETURNS bool
    AS $$
        UPDATE o_test_1 SET val_2 = val_2 + 1; 
        SELECT true;
    $$ LANGUAGE sql;
}

teardown
{
    DROP TABLE o_test_1;
    DROP FUNCTION func_1(int);
}

session s1

step "begin_1" { BEGIN ISOLATION LEVEL READ COMMITTED; }
step "update_1"  { UPDATE o_test_1 SET val_2 = val_2 - 2; }
step "commit_1"    { COMMIT; }

session s2

step "begin_2"  { BEGIN ISOLATION LEVEL READ COMMITTED; }
step "with_2"  { WITH test AS 
                      (UPDATE o_test_1 
                      SET val_2 = val_2 + 1 
                      WHERE val_1 = 'a' RETURNING *, 
                        func_1(999)) 
                      UPDATE o_test_1 a SET val_2 = test.val_2 + 1 
                        FROM test RETURNING *; }
step "commit_2"  { COMMIT; }

session s3

step "begin_3"    { BEGIN ISOLATION LEVEL READ COMMITTED; }
step "select_3"  { SELECT * FROM o_test_1 ORDER BY val_1; }
teardown  { COMMIT; }

permutation "begin_1" "begin_2" "begin_3" "update_1" "with_2" "commit_1" "commit_2" "select_3"