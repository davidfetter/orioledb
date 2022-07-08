
setup

{
    CREATE EXTENSION IF NOT EXISTS orioledb;

    CREATE TABLE o_test_1(
        val_1 text PRIMARY KEY, 
        val_2 text
    )USING orioledb;

    CREATE FUNCTION func_1(p_comment text, p_a anynonarray, p_op text, p_b anynonarray)
    RETURNS bool AS $$
        DECLARE
            r bool;
        BEGIN
            EXECUTE format('SELECT $1 %s $2', p_op) INTO r USING p_a, p_b;
        RETURN r;
        END; 
    $$ LANGUAGE plpgsql;

    CREATE FUNCTION func_2() RETURNS TRIGGER AS $$
        DECLARE
          r_new text;
          r_old text;
          r_ret record;
    BEGIN
        IF TG_OP = 'UPDATE' THEN
            r_old = OLD;
            r_new = NEW;
            r_ret = NEW;
        END IF;
	RETURN r_ret;
    END;
    $$ LANGUAGE plpgsql;
}

teardown
{
     DROP TABLE o_test_1;
     DROP FUNCTION func_1(text, anynonarray, text, anynonarray);
     DROP FUNCTION func_2();
}


session s1

step "begin_1"     { BEGIN ISOLATION LEVEL READ COMMITTED; }
step "commit_1"     { COMMIT; }

step "trig_1" { CREATE TRIGGER trig_1 BEFORE UPDATE 
                            ON o_test_1 
                            FOR EACH ROW EXECUTE PROCEDURE func_1(); }

step "trig_1_2" { CREATE TRIGGER trig_2 AFTER UPDATE 
                            ON o_test_1 
                            FOR EACH ROW EXECUTE PROCEDURE func_2(); }

step "insert_1" { INSERT INTO o_test_1 
                    VALUES ('key-a', 'val-a-s1') RETURNING *; }

step "insert_1_2" { INSERT INTO o_test_1 
                    VALUES ('key-b', 'val-b-s1') RETURNING *; }


step "update_1" { UPDATE o_test_1 SET val_2 = val_2 || '-ups1'
                      WHERE
                          func_1('upd', val_1, '=', 'key-a')
                     RETURNING *; }


session s2

step "begin_2"     { BEGIN ISOLATION LEVEL READ COMMITTED; SELECT 1; }
step "commit_2"     { COMMIT; }

step "update_2" { UPDATE o_test_1 SET val_2 = val_2 || '-ups2'
                      WHERE
                          func_1('upd', val_1, '=', 'key-a')
                      RETURNING *; }

permutation "trig_1" "trig_1_2" "insert_1" "insert_1_2" "begin_1" "begin_2" "update_1" "update_2" "commit_1" "commit_2"
