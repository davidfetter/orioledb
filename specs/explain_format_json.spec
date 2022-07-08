#ERROR  Only text explain format now supported

setup { CREATE EXTENSION IF NOT EXISTS orioledb;

    CREATE OR REPLACE FUNCTION func_1(val text)
    RETURNS json
    LANGUAGE plpgsql AS $$
        DECLARE
            a json;
        BEGIN
            EXECUTE val INTO STRICT a;
            RETURN a;
        END;$$; }

teardown { DROP FUNCTION func_1(text); }

session s_1

step "begin_repeatable_read_1" { BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ; }
step "begin_1" { COMMIT; }


session s_2

step "begin_2" { BEGIN; }
step "commit_2" { COMMIT; }
step "create_table_2" { CREATE TEMPORARY TABLE o_test_2 (
                            val_1 int unique
                          )USING orioledb;

                          INSERT INTO o_test_2(val_1) 
                            VALUES(1),(2); }


step "drop_2" { DROP TABLE o_test_2; }
step "delete_2" { DELETE FROM o_test_2; }
step "select_func_1" { SELECT func_1($$
                      EXPLAIN (FORMAT json, BUFFERS, ANALYZE)
	                  SELECT * FROM o_test_2 ORDER BY val_1;$$)->0->'Plan'->'Heap Fetches'; }


permutation "create_table_2" "begin_repeatable_read_1" "select_func_1" "select_func_1" "begin_1" "delete_2" "select_func_1" "select_func_1" "begin_2" "commit_2" "drop_2"
permutation "create_table_2" "begin_repeatable_read_1" "select_func_1" "select_func_1" "delete_2" "select_func_1" "select_func_1" "begin_2" "drop_2"
