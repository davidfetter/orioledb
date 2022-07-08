
setup
{
    CREATE EXTENSION IF NOT EXISTS orioledb;

    CREATE TABLE o_test_1 (
        val_1 text PRIMARY KEY, 
        val_2 numeric not null
    )USING orioledb;

    INSERT INTO o_test_1 
        VALUES ('a', 600), ('b', 600);

 
}

teardown
{
 DROP TABLE o_test_1;
 }

session s1
step "begin_1" { BEGIN ISOLATION LEVEL READ COMMITTED; }
step "update_1"	{ UPDATE o_test_1 SET val_2 = val_2 - 200; }
step "commit_1"		{ COMMIT; }


session s2
step "begin_2"	{ BEGIN ISOLATION LEVEL READ COMMITTED; }
step "update_2"	{ UPDATE o_test_1 SET val_2 = val_2 + 450; }
step "commit_2"	{ COMMIT; }

permutation "begin_1" "begin_2" "update_1" "update_2" "commit_1" "commit_2"