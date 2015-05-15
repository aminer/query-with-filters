/*******************************************************************************
 * Copyright 2008-2013 by Aerospike.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 ******************************************************************************/


//==========================================================
// Includes
//

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <inttypes.h>

#include <aerospike/aerospike.h>
#include <aerospike/aerospike_key.h>
#include <aerospike/aerospike_query.h>
#include <aerospike/as_error.h>
#include <aerospike/as_integer.h>
#include <aerospike/as_key.h>
#include <aerospike/as_map.h>
#include <aerospike/as_query.h>
#include <aerospike/as_record.h>
#include <aerospike/as_status.h>
#include <aerospike/as_val.h>
#include <aerospike/as_arraylist.h>

#include "example_utils.h"


//==========================================================
// Constants
//

#define UDF_MODULE "profile"
#define UDF_USER_PATH "../udf/"
const char UDF_FILE_PATH[] =  UDF_USER_PATH UDF_MODULE ".lua";

const char USERNAME_INDEX_NAME[] = "profileindex";


//==========================================================
// Forward Declarations
//

bool query_cb(const as_val* p_val, void* udata);
bool query_cb_map(const as_val* p_val, void* udata);
void cleanup(aerospike* p_as);
bool insert_records(aerospike* p_as);


//==========================================================
// QUERY WITH MULTIPLE FILTERS Example
//

int
main(int argc, char* argv[])
{
	// Parse command line arguments.
	if (! example_get_opts(argc, argv, EXAMPLE_MULTI_KEY_OPTS)) {
		exit(-1);
	}

	// Connect to the aerospike database cluster.
	aerospike as;
	example_connect_to_aerospike_with_udf_config(&as, UDF_USER_PATH);

	// Start clean.
	example_remove_test_records(&as);
	example_remove_index(&as, USERNAME_INDEX_NAME);

	LOG("\nregister %s", UDF_FILE_PATH);

	// Register the UDF in the database cluster.
	if (! example_register_udf(&as, UDF_FILE_PATH)) {
		example_cleanup(&as);
		exit(-1);
	}

	as_error err;
	as_index_task task;

	LOG("create index %s", USERNAME_INDEX_NAME);

	if (aerospike_index_create(&as, &err, &task, NULL, g_namespace, g_set, "username", USERNAME_INDEX_NAME, AS_INDEX_STRING) == AEROSPIKE_OK) {
    	if (aerospike_index_create_wait(&err, &task, 0) != AEROSPIKE_OK) {
    		LOG("error(%d): %s", err.code, err.message);
    	}
    } else {
    	LOG("error(%d): %s", err.code, err.message);
    }

    LOG("insert records");

	if (! insert_records(&as)) {
		cleanup(&as);
		exit(-1);
	}

	LOG("\nread records");

	if (! example_read_test_records(&as)) {
		cleanup(&as);
		exit(-1);
	}

	// Create an as_query object.
	as_query query;
	as_query_init(&query, g_namespace, g_set);

	// Generate an as_query.where condition. Note that as_query_destroy() takes
	// care of destroying all the query's member objects if necessary. However
	// using as_query_where_inita() does avoid internal heap usage.
	as_query_where_inita(&query, 1);
	as_query_where(&query, "username", as_string_equals("Mary"));

	LOG("\nexecuting query where username = Mary");

	// Execute the query. This call blocks - callbacks are made in the scope of
	// this call.
	if (aerospike_query_foreach(&as, &err, NULL, &query, query_cb, NULL) != AEROSPIKE_OK) {
		LOG("aerospike_query_foreach() returned %d - %s", err.code, err.message);
		as_query_destroy(&query);
		cleanup(&as);
		exit(-1);
	}

	// Reuse the as_query object for another query.
	as_query_destroy(&query);
	as_query_init(&query, g_namespace, g_set);

	// Aggregate profile.check_password('ghjks') on test.profile where username = 'Mary'
 	as_arraylist args;
	as_arraylist_init(&args, 1, 0);
	as_arraylist_append_str(&args, "ghjks");

	as_query_apply(&query, UDF_MODULE, "check_password", (as_list *) &args);

	LOG("\nexecuting filter query where password = ghjks");

	// Execute the query. This call blocks - callbacks are made in the scope of
	// this call.
	if (aerospike_query_foreach(&as, &err, NULL, &query, query_cb_map, NULL) != AEROSPIKE_OK) {
		LOG("aerospike_query_foreach() returned %d - %s", err.code, err.message);
		as_query_destroy(&query);
		cleanup(&as);
		exit(-1);
	}

	as_query_destroy(&query);

	// Cleanup and disconnect from the database cluster.
	cleanup(&as);

	LOG("\nquery with multiple filters example successfully completed");

	return 0;
}


//==========================================================
// Query Callback
//

bool
query_cb_map(const as_val* p_val, void* udata)
{
	if (! p_val) {
		LOG("query callback returned null - query is complete");
		return true;
	}

	// Because of the UDF used, we expect an as_map to be returned.
	if (! as_map_fromval(p_val)) {
		LOG("query callback returned non-as_map object");
		return true;
	}

	// The map keys are number tokens ("1" to "10") and each value is the total
	// number of occurrences of the token in the records aggregated.
	char* val_as_str = as_val_tostring(p_val);

	LOG("query callback returned %s", val_as_str);
	free(val_as_str);

	return true;
}

bool
query_cb(const as_val* p_val, void* udata)
{
	if (! p_val) {
		LOG("query callback returned null - query is complete");
		return true;
	}

	// The query didn't use a UDF, so the as_val object should be an as_record.
	as_record* p_rec = as_record_fromval(p_val);

	if (! p_rec) {
		LOG("query callback returned non-as_record object");
		return true;
	}

	LOG("query callback returned record:");
	example_dump_record(p_rec);

	return true;
}


//==========================================================
// Helpers
//

void
cleanup(aerospike* p_as)
{
	example_remove_test_records(p_as);
	example_remove_index(p_as, USERNAME_INDEX_NAME);
	example_cleanup(p_as);
}

bool
insert_records(aerospike* p_as)
{
	as_record rec;
	as_record_inita(&rec, 2);
	as_error err;
	as_key key;

	as_key_init_int64(&key, g_namespace, g_set, 1);
	as_record_set_str(&rec, "username", "Charlie");
	as_record_set_str(&rec, "password", "cpass");
	if (aerospike_key_put(p_as, &err, NULL, &key, &rec) != AEROSPIKE_OK) {
		LOG("aerospike_key_put() returned %d - %s", err.code, err.message);
		return false;
	}
	
	as_key_init_int64(&key, g_namespace, g_set, 2);
	as_record_set_str(&rec, "username", "Bill");
	as_record_set_str(&rec, "password", "hknfpkj");
	if (aerospike_key_put(p_as, &err, NULL, &key, &rec) != AEROSPIKE_OK) {
		LOG("aerospike_key_put() returned %d - %s", err.code, err.message);
		return false;
	}

	as_key_init_int64(&key, g_namespace, g_set, 3);
	as_record_set_str(&rec, "username", "Doug");
	as_record_set_str(&rec, "password", "dj6554");
	if (aerospike_key_put(p_as, &err, NULL, &key, &rec) != AEROSPIKE_OK) {
		LOG("aerospike_key_put() returned %d - %s", err.code, err.message);
		return false;
	}

	as_key_init_int64(&key, g_namespace, g_set, 4);
	as_record_set_str(&rec, "username", "Mary");
	as_record_set_str(&rec, "password", "ghjks");
	if (aerospike_key_put(p_as, &err, NULL, &key, &rec) != AEROSPIKE_OK) {
		LOG("aerospike_key_put() returned %d - %s", err.code, err.message);
		return false;
	}

	as_key_init_int64(&key, g_namespace, g_set, 5);
	as_record_set_str(&rec, "username", "Julie");
	as_record_set_str(&rec, "password", "zzxzxvv");
	if (aerospike_key_put(p_as, &err, NULL, &key, &rec) != AEROSPIKE_OK) {
		LOG("aerospike_key_put() returned %d - %s", err.code, err.message);
		return false;
	}

	LOG("insert succeeded");

	return true;
}