setup
{
    CREATE EXTENSION IF NOT EXISTS orioledb;

    CREATE TABLE o_test_1(
      val_1 text not null, 
      val_2 text
    )USING orioledb;
    
    CREATE UNIQUE INDEX ON o_test_1(lower(val_1));
}

teardown
{
  DROP TABLE o_test_1;
}

session s1
step "begin_1" { BEGIN ISOLATION LEVEL READ COMMITTED; }
step "insert_1" { INSERT INTO o_test_1(val_1, val_2) 
                  VALUES('Aa', 'b1'); }
step "commit_1" { COMMIT; }

session s2
step "begin_2" { BEGIN ISOLATION LEVEL READ COMMITTED; }
step "insert_2" { INSERT INTO o_test_1(val_1, val_2) 
                  VALUES('AA', 'b2') 
                  ON CONFLICT (lower(val_1)) 
                  DO UPDATE set val_1 = EXCLUDED.val_1, val_2 = o_test_1.val_2; }
step "select_2" { SELECT * FROM o_test_1; }
step "commit_2" { COMMIT; }

permutation "begin_1" "begin_2" "insert_1" "insert_2" "commit_1" "select_2" "commit_2"
