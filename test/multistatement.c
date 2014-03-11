/*
  Copyright (c) 2001, 2012, Oracle and/or its affiliates. All rights reserved.
                2013 MontyProgram AB

  The MySQL Connector/ODBC is licensed under the terms of the GPLv2
  <http://www.gnu.org/licenses/old-licenses/gpl-2.0.html>, like most
  MySQL Connectors. There are special exceptions to the terms and
  conditions of the GPLv2 as it is applied to this software, see the
  FLOSS License Exception
  <http://www.mysql.com/about/legal/licensing/foss-exception.html>.
  
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published
  by the Free Software Foundation; version 2 of the License.
  
  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
  or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
  for more details.
  
  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA
*/

#include "tap.h"

ODBC_TEST(test_multi_statements)
{
  long num_inserted;
  SQLRETURN rc;

  OK_SIMPLE_STMT(Stmt, "DROP TABLE IF EXISTS t1");
  OK_SIMPLE_STMT(Stmt, "CREATE TABLE t1 (a int)");

  OK_SIMPLE_STMT(Stmt, "INSERT INTO t1 VALUES(1);INSERT INTO t1 VALUES(2)");

  SQLRowCount(Stmt, &num_inserted);
  diag("inserted: %d", num_inserted);
  FAIL_IF(num_inserted != 1, "Expected 1 row inserted");
  
  rc= SQLMoreResults(Stmt);
  num_inserted= 0;
  rc= SQLRowCount(Stmt, &num_inserted);
  FAIL_IF(num_inserted != 1, "Expected 1 row inserted");

  rc= SQLMoreResults(Stmt);
 FAIL_IF(rc != SQL_NO_DATA, "expected no more results");

  return OK;
}

ODBC_TEST(test_multi_on_off)
{
  SQLHENV myEnv;
  SQLHDBC myDbc;
  SQLHSTMT myStmt;
  SQLRETURN rc;

  my_options= 0;
  ODBC_Connect(&myEnv, &myDbc, &myStmt);

  rc= SQLPrepare(myStmt, "DROP TABLE IF EXISTS t1; CREATE TABLE t1(a int)", SQL_NTS);
  FAIL_IF(SUCCEEDED(rc), "Error expected"); 

  ODBC_Disconnect(myEnv, myDbc, myStmt);

  my_options= 67108866;
  ODBC_Connect(&myEnv, &myDbc, &myStmt);

  rc= SQLPrepare(myStmt, "DROP TABLE IF EXISTS t1; CREATE TABLE t1(a int)", SQL_NTS);
  FAIL_IF(!SUCCEEDED(rc), "Success expected"); 

  ODBC_Disconnect(myEnv, myDbc, myStmt);
  return OK;
}

ODBC_TEST(test_params)
{
  SQLHENV myEnv;
  SQLHDBC myDbc;
  SQLHSTMT myStmt;
  SQLRETURN rc;
  int i,j;

  rc= SQLExecDirect(Stmt, "DROP TABLE IF EXISTS t1; CREATE TABLE t1(a int)", SQL_NTS);
  FAIL_IF(!SUCCEEDED(rc), "unexpected error"); 

  rc= SQLExecDirect(Stmt, "DROP TABLE IF EXISTS t2; CREATE TABLE t2(a int)", SQL_NTS);
  FAIL_IF(!SUCCEEDED(rc), "unexpected error"); 

  rc= SQLPrepare(Stmt, "INSERT INTO t1 VALUES (?); INSERT INTO t2 VALUES (?)", SQL_NTS);
  CHECK_STMT_RC(Stmt, rc);

  rc= SQLBindParam(Stmt, 1, SQL_C_LONG, SQL_INTEGER, 10, 0, &i, NULL);
  FAIL_IF(!SUCCEEDED(rc), "unexpected error"); 
  rc= SQLBindParam(Stmt, 2, SQL_C_LONG, SQL_INTEGER, 10, 0, &j, NULL);
  FAIL_IF(!SUCCEEDED(rc), "unexpected error"); 

  for (i=0; i < 100; i++)
  {
    j= i + 100;
    rc= SQLExecute(Stmt);
    CHECK_STMT_RC(Stmt, rc); 

    while (SQLMoreResults(Stmt) == SQL_SUCCESS);
  }

  return OK;
}

MA_ODBC_TESTS my_tests[]=
{
  {test_multi_statements, "test_multi_statements"},
  {test_multi_on_off, "test_multi_on_off"},
//  {test_noparams, "test_noparams"},
  {test_params, "test_params"},
  {NULL, NULL}
};

int main(int argc, char **argv)
{
  int tests= sizeof(my_tests)/sizeof(MA_ODBC_TESTS) - 1;
  my_options= 67108866;
  get_options(argc, argv);
  plan(tests);
  return run_tests(my_tests);
}