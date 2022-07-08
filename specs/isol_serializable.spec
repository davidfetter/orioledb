#ERROR:  could not serialize access due to read/write 
#dependencies among transactions without USING orioledb,
#but with orioledb is ok

setup
{
    CREATE EXTENSION IF NOT EXISTS orioledb;

    CREATE TABLE o_test_1(
        val_1 int,
        val_2 int
    )USING orioledb;

    INSERT INTO o_test_1 
        VALUES (1, 2), 
               (3, 4), 
               (5, 6);

     CREATE TABLE o_test_2 (
        val_3 int, 
        val_4 int
    )USING orioledb;

    CREATE TABLE o_test_3(
        val_1 int, 
        val_2 int
    )USING orioledb;

    INSERT INTO o_test_3 
        VALUES (1, 600),(2,600);

    CREATE TABLE o_test_4 (
        val_1 int
    )USING orioledb;

    CREATE TABLE o_test_5 (
        val_2 int
    )USING orioledb;

    INSERT INTO o_test_4 
        VALUES (1);
    INSERT INTO o_test_5
        VALUES (1);

  CREATE TABLE o_test_6 (
        val_1 int
    )USING orioledb;
    
    CREATE FUNCTION func_1() RETURNS TRIGGER 
    AS $$
        BEGIN
        PERFORM TRUE FROM o_test_6
            WHERE val_1 = OLD.val_1;
        IF TG_OP = 'DELETE' THEN
            RETURN OLD;
        END IF;
            RETURN NEW;
        END;
    $$ LANGUAGE plpgsql;

    CREATE TRIGGER trig_1 BEFORE UPDATE OR DELETE
        ON o_test_6
        FOR EACH ROW EXECUTE FUNCTION func_1();

    CREATE FUNCTION func_2() RETURNS TRIGGER AS $$
    BEGIN
        PERFORM TRUE FROM o_test_6 WHERE val_1 = NEW.val_1;
        IF NOT FOUND THEN
            RETURN NEW;
        END IF; 
            RETURN NEW;
    END;
    $$ LANGUAGE plpgsql;

    CREATE TRIGGER trig_2 BEFORE INSERT OR UPDATE
        ON o_test_6
        FOR EACH ROW EXECUTE FUNCTION func_2();
    
    INSERT INTO o_test_6
        VALUES(0);

  CREATE TABLE o_test_7(
    val_1 int, 
    val_2 int, 
    val_3 int)USING orioledb;

  INSERT INTO o_test_7
    VALUES (1, 2, 3);
}

teardown
{
    DROP TABLE o_test_1, o_test_2, o_test_3, o_test_4,
                o_test_5, o_test_6, o_test_7;

    DROP FUNCTION func_1();
    DROP FUNCTION func_2();
}

session s1

step "begin_1" { BEGIN ISOLATION LEVEL SERIALIZABLE; }

step "update_1_t1" { UPDATE o_test_1 SET val_2 = 4 WHERE val_2 = 6; }
step "select_1_t1" { SELECT * FROM o_test_1 WHERE val_1 = 1; }
step "insert_1_t2" { INSERT INTO o_test_2 VALUES (3, 4); }
step "update_1_t3" { UPDATE o_test_3 SET val_2 = val_2 - 200 WHERE val_1 = 1; }
step "select_1_t3" { SELECT SUM(val_2) FROM o_test_3; }
step "update_1_t4" { UPDATE o_test_4 SET val_1 = val_1 + 1; }
step "delete_1_t5" { DELETE FROM o_test_5 WHERE val_2 = (SELECT min(val_1) FROM o_test_1); }
step "insert_1_t6" { INSERT INTO o_test_6 (val_1) VALUES (0); }
step "select_1_t7" { SELECT * FROM o_test_7; }
step "insert_1_t7" { INSERT INTO o_test_7 VALUES (1, 6, 5); }
step "update_1_t7" { UPDATE o_test_7 SET val_1 = val_1 + 1; }

step "commit_1" { COMMIT; }

session s2

step "begin_2" { BEGIN ISOLATION LEVEL SERIALIZABLE; }

step "update_2_t1" { UPDATE o_test_1 SET val_2 = 6 WHERE val_2 = 4}
step "delete_2_t1"	{ DELETE FROM o_test_1 WHERE val_1 = 1; }
step "delete_where_2_t1" { DELETE FROM o_test_1 WHERE val_1 = (SELECT min(val_2) FROM o_test_5); }
step "insert_2_t1" { INSERT INTO o_test_1 SELECT 42, 44; }
step "update_2_t1_7" { UPDATE o_test_1 SET val_2 = (SELECT val_1 + 1 FROM o_test_7); }
step "select_2_t2"	{ SELECT * FROM o_test_2 WHERE val_4 = 2; }
step "update_2_t3"	{ UPDATE o_test_3 SET val_2 = val_2 - 200 WHERE val_1 = 2; }
step "select_2_t3"	{ SELECT SUM(val_2) FROM o_test_3; }
step "update_2_t5"	{ UPDATE o_test_5 SET val_2 = (SELECT val_2 + 1 FROM o_test_4); }
step "delete_2_t6"	{ DELETE FROM o_test_6 WHERE val_1 = 0; }
step "update_2_t7"	{ UPDATE o_test_7 SET val_2 = 8; }
step "select_2_t7"	{ SELECT * FROM o_test_7; }

step "commit_2" { COMMIT; }

session s3

step "begin_3" { BEGIN ISOLATION LEVEL SERIALIZABLE; }

step "update_3_t1" { UPDATE o_test_1 SET val_2 = 1 WHERE val_1 = 50; }
step "select_3_t1" { SELECT val_2 FROM o_test_1; }
step "update_3_t2" { UPDATE o_test_2 SET val_4 = 6 WHERE val_3 = 7; }
step "delete_3_t2" { DELETE FROM o_test_2 WHERE val_3 = 7; }
step "select_3_t2" { SELECT val_2 FROM o_test_5; }

step "abort_3"     { ABORT; }
step "commit_3"		{ COMMIT; }

session s4

step "begin_4" { BEGIN ISOLATION LEVEL SERIALIZABLE; }

step "update_4_t1" { UPDATE o_test_1 SET val_2 = 1 WHERE val_1 = 5; }
step "select_4_t1" { SELECT * FROM o_test_1 WHERE val_1 = 1; }

step "commit_4"	{ COMMIT; }


permutation "begin_1" "begin_2" "update_1_t1" "update_2_t1" "commit_1" "commit_2"
permutation "begin_1" "begin_2" "update_2_t1" "update_1_t1" "commit_1" "commit_2"

permutation "begin_1" "begin_2" "select_1_t1" "insert_1_t2" "select_2_t2" "delete_2_t1" "commit_1" "commit_2"
permutation "begin_1" "begin_2" "select_1_t1" "select_2_t2" "insert_1_t2" "commit_1" "delete_2_t1" "commit_2"
permutation "begin_1" "begin_2" "select_1_t1" "select_2_t2" "insert_1_t2" "delete_2_t1" "commit_1" "commit_2"
permutation "begin_1" "begin_2" "select_1_t1" "select_2_t2" "delete_2_t1" "insert_1_t2" "commit_1" "commit_2"
permutation "begin_1" "begin_2" "select_1_t1" "select_2_t2" "delete_2_t1" "commit_2" "insert_1_t2" "commit_1"
permutation "begin_1" "begin_2" "select_2_t2" "select_1_t1" "insert_1_t2" "commit_1" "delete_2_t1" "commit_2"
permutation "begin_1" "begin_2" "select_2_t2" "select_1_t1" "insert_1_t2" "delete_2_t1" "commit_1" "commit_2"
permutation "begin_1" "begin_2" "select_2_t2" "select_1_t1" "delete_2_t1" "insert_1_t2" "commit_1" "commit_2"
permutation "begin_1" "begin_2" "select_2_t2" "select_1_t1" "delete_2_t1" "commit_2" "insert_1_t2" "commit_1"
permutation "begin_1" "begin_2" "select_2_t2" "delete_2_t1" "select_1_t1" "insert_1_t2" "commit_1" "commit_2"
permutation "begin_1" "begin_2" "select_2_t2" "delete_2_t1" "select_1_t1" "commit_2" "insert_1_t2" "commit_1"

permutation "begin_1" "begin_2" "update_1_t3" "select_1_t3" "update_2_t3" "select_2_t3" "commit_1" "commit_2"
permutation "begin_1" "begin_2" "update_1_t3" "update_2_t3" "select_1_t3" "select_2_t3" "commit_1" "commit_2"
permutation "begin_1" "begin_2" "update_1_t3" "update_2_t3" "select_2_t3" "select_1_t3" "commit_1" "commit_2"
permutation "begin_1" "begin_2" "update_1_t3" "update_2_t3" "select_2_t3" "commit_2" "select_1_t3" "commit_1"
permutation "begin_1" "begin_2" "update_2_t3" "update_1_t3" "select_1_t3" "commit_1" "select_2_t3" "commit_2"
permutation "begin_1" "begin_2" "update_2_t3" "update_1_t3" "select_1_t3" "select_2_t3" "commit_1" "commit_2"
permutation "begin_1" "begin_2" "update_2_t3" "update_1_t3" "select_2_t3" "select_1_t3" "commit_1" "commit_2"
permutation "begin_1" "begin_2" "update_2_t3" "update_1_t3" "select_2_t3" "commit_2" "select_1_t3" "commit_1"
permutation "begin_1" "begin_2" "update_2_t3" "select_2_t3" "update_1_t3" "select_1_t3" "commit_1" "commit_2"

permutation "begin_1" "begin_2" "begin_3" "update_1_t4" "update_2_t5" "select_3_t2" "commit_1" "commit_2" "commit_3"
permutation "begin_1" "begin_2" "begin_3" "update_1_t4" "select_3_t2" "update_2_t5" "commit_1" "commit_2" "commit_3"
permutation "begin_1" "begin_2" "begin_3" "update_2_t5" "update_1_t4" "commit_1" "select_3_t2" "commit_2" "commit_3"
permutation "begin_1" "begin_2" "begin_3" "update_2_t5" "update_1_t4" "select_3_t2" "commit_1" "commit_2" "commit_3"
permutation "begin_1" "begin_2" "begin_3" "update_2_t5" "select_3_t2" "update_1_t4" "commit_1" "commit_2" "commit_3"
permutation "begin_1" "begin_2" "begin_3" "select_3_t2" "update_1_t4" "update_2_t5" "commit_1" "commit_2" "commit_3"
permutation "begin_1" "begin_2" "begin_3" "select_3_t2" "update_2_t5" "update_1_t4" "commit_1" "commit_2" "commit_3"

permutation "begin_1" "begin_2" "delete_1_t5" "delete_where_2_t1" "commit_1" "commit_2"
permutation "begin_1" "begin_2" "delete_where_2_t1" "delete_1_t5" "commit_1" "commit_2"

permutation "begin_1" "begin_2" "begin_3" "select_1_t1" "insert_2_t1" "insert_1_t2" "commit_1" "update_3_t2" "select_2_t2"  "commit_2" "abort_3"
permutation "begin_1" "begin_2" "begin_3" "select_1_t1" "insert_2_t1" "insert_1_t2" "commit_1" "delete_3_t2" "select_2_t2"  "commit_2" "abort_3"

permutation "begin_1" "begin_2" "insert_1_t6" "delete_2_t6" "commit_1" "commit_2"
permutation "begin_1" "begin_2" "delete_2_t6" "insert_1_t6" "commit_1" "commit_2"

permutation "begin_1" "begin_2" "select_1_t7" "insert_1_t7" "select_2_t7" "update_2_t7" "commit_1" "commit_2"
permutation "begin_1" "begin_2" "select_1_t7" "select_2_t7" "insert_1_t7" "update_2_t7" "commit_1" "commit_2"
permutation "begin_1" "begin_2" "select_1_t7" "select_2_t7" "update_2_t7" "insert_1_t7" "commit_1" "commit_2"
permutation "begin_1" "begin_2" "select_2_t7" "select_1_t7" "insert_1_t7" "update_2_t7" "commit_1" "commit_2"
permutation "begin_1" "begin_2" "select_2_t7" "select_1_t7" "update_2_t7" "insert_1_t7" "commit_1" "commit_2"
permutation "begin_1" "begin_2" "select_2_t7" "update_2_t7" "select_1_t7" "insert_1_t7" "commit_1" "commit_2"

permutation "begin_1" "begin_2" "begin_3" "update_1_t7" "update_2_t1_7" "commit_1" "select_3_t1" "commit_2" "commit_3"
permutation "begin_1" "begin_2" "begin_3" "update_1_t7" "update_2_t1_7" "select_3_t1" "commit_1" "commit_2" "commit_3"
permutation "begin_1" "begin_2" "begin_3" "update_1_t7" "select_3_t1" "update_2_t1_7" "commit_1" "commit_2" "commit_3"
permutation "begin_1" "begin_2" "begin_3" "update_2_t1_7" "update_1_t7" "commit_1" "select_3_t1" "commit_2" "commit_3"
permutation "begin_1" "begin_2" "begin_3" "update_2_t1_7" "update_1_t7" "select_3_t1" "commit_1" "commit_2" "commit_3"
permutation "begin_1" "begin_2" "begin_3" "update_2_t1_7" "select_3_t1" "update_1_t7" "commit_1" "commit_2" "commit_3"
permutation "begin_1" "begin_2" "begin_3" "select_3_t1" "update_1_t7" "update_2_t1_7" "commit_1" "commit_2" "commit_3"
permutation "begin_1" "begin_2" "begin_3" "select_3_t1" "update_2_t1_7" "update_1_t7" "commit_1" "commit_2" "commit_3"

