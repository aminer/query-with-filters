#Query with multiple filters
##Problem
I would like to Query Aerospike with multiple predicates, although Aerospike supports querying only a single index.
##Solution
Aerospike has an Aggregation feature that can be used to solve this problem. Aggregations operate on, or process, a stream of data using StreamUDFs. A stream is the output of a query on a secondary index. Imagine a stream of records flowing out of a query and passing through one or more StreamUDFs, and the result set being passed back to your application. See Aggregation Guide for more detailed information.
###The code
The code for this example is in GitHub https://github.com/aerospike/query-with-filters.

The Aerospike Query Language script is in the subdirectory "aql". The file "AuthenticateTest.aql" contains aql that creates a secondary index, inserts data, registers the StreamUDF package and executes the aggregation query.

To run the aql, issue the following command on a Linux machine where Aerospike is installed:
```
aql -f aql/AuthenticateTest.aql
```
The Lua package containing the StreamUDF is located in the "udf" subdirectory. The file is "profile.lua"
The Java code that does the same as the AQL, is located under the "src" subdirectory. This code can be built with the maven command:
```
mvn clean package
```
##Discussion
To illustrate this use, let’s construct a scenario. You want to authenticate a user, which is done with a username and password. You have user profiles in a Set “profile” and each user profile record has Bins: “username” and “password”. You wish to return a user record where both the username and password match the user’s authentication, even if there are multiple users with the same username.

We will choose “username” as the appropriate index, and create a filter that matches on “password”.
Let’s create an index and insert some data that matches this scenario. Create a secondary index on “username”. An easy way to do this is to use AQL. Run AQL on a Linux machine and enter the following command:
```sql
create index profileindex on test.profile (username) string
```
Then insert a number of records:
```sql
insert into test.profile (PK, username, password) values ('1', 'Charlie', 'cpass')
insert into test.profile (PK, username, password) values ('2', 'Bill', 'hknfpkj')
insert into test.profile (PK, username, password) values ('3', 'Doug', 'dj6554')
insert into test.profile (PK, username, password) values ('4', 'Mary', 'ghjks')
insert into test.profile (PK, username, password) values ('5', 'Julie', 'zzxzxvv')
```
As the records are written, “username” Bin will be included in the secondary index “profileindex”. You can now query based on that index.
AQL example:
```sql
select * from test.profile where username = 'Mary'
```
Java example:
```java
Key key;
Record record;
Statement stmt = new Statement();
stmt.setNamespace("test");
stmt.setSetName("profile");
stmt.setFilters(Filter.equal("username", Value.get("mary")));
// Execute the query
RecordSet recordSet = client.query(null, stmt);

// Process the record set
try {
    while (recordSet != null && recordSet.next()) {
        key = recordSet.getKey();
        record = recordSet.getRecord();

        System.out.println("Record: " + record);

    }
} finally {
recordSet.close();
}
```
Both the AQL and the java code are semantically equivalent. They perform a simple secondary index query on “username”, but do not filter on “password”. The result is the record(s) with the matching username.

Data has been inserted, and you have done a simple query. This is only the first part of the solution.
StreamUDF

The second part of the solution is to add a filter on “password”. To do this you will need to use Aggregations and write a StreamUDF in Lua.

StreamUDF can be a little baffling, but not really, consider this diagram:
![Stream processing](query_stream_filter.png)

The output of the query is a stream of records. This stream can be processed by zero or more of filters. As the Query runs on all nodes in the cluster, so does the filter function.

The Map function is a mechanism to collect elements from the stream to be returned, in the following code you will see that the map_profile function returns the "name" Bin value and the "password" Bin value.

Finally you can supply a reduce function that can aggregate values from the map function(s). It receives lists of values from map functions that were run in parallel, and emits a result.The reduce function runs on each node in the cluster, in parallel, and the final reduce is done on the client where it aggregates the results from each node.

We will use the query (described above) to locate the correct user name, then we will pass the output of the query (the stream) through a filter that compares the password parameter to the value stored in the record. The filter returns a boolean value, true if the record should be passed on in the stream, false if the record should be filtered out. 

Lets take a look at the complete StreamUDF written in Lua. Paste this example into a file profile.lua .
```lua
local function map_profile(record)
  -- Add user and password to returned map.
  -- Could add other record bins here as well.
  return map {name=record.name, password=record.password}
end

function check_password(stream,password)
  local function filter_password(record)
    return record.password == password
  end
  return stream : filter(filter_password) : map(map_profile)
end
```
The function check_password configures how the aggregation will be applied to the stream. You can see that the first parameter to this function is the stream. Look at the return statement
```lua
return stream : filter(filter_password) : map(map_profile)
```
This line configures the function ```filter_password``` as a filter function, and map function ```map_profile```. 
The function filter returns a stream that has been filtered by the ```filter_password function```. The function map returns a stream that contains the values produced by ```map_profile``` function.

Next it configures the function ```map_profile``` as a map function and applies it to the stream after the filter.

The ```check_password``` function receives the output of the query in the ```stream``` parameter, and the password in the ```password``` parameter. This function returns stream that has been processed by the filter and map functions. In our case, the final stream will contain only the matched username and password if successful and nothing if unsuccessful.
###Register your StreamUDF
Before you can use the StreamUDF, you must register it. The above code is stored in a file named 'profile.lua'. To register this Lua package as a UDF, execute the following aql:
register module 'profile.lua'

Using Aerospike’s distributed algorithm, this file is installed on all nodes in the Aerospike cluster. The file persists in this version even as nodes are added and removed.
Running the query with the filter
Here is how you run the aggregation query in AQL:
```sql
aggregate profile.check_password('ghjks') on test.profile where username = 'Mary'
```
Where profile is the UDF package, check_password is the function to be called with the parameter of 'ghjks' which is the user’s password, test.profile is the namespace and set where the data is stored and finally username is the Bin name used in the query. 

The same functionality in Java:
```java
stmt = new Statement();
stmt.setNamespace("test");
stmt.setSetName("profile");
stmt.setFilters(Filter.equal("username", Value.get("Mary")));
resultSet = client.queryAggregate(null, stmt, 
    "profile", "check_password" , Value.get("ghjks"));

try {
    int count = 0;

    while (resultSet.next()) {
        Object object = resultSet.getObject();
        System.out.println("Result: " + object);
        count++;
    }

    if (count == 0) {
        System.out.println("No results returned.");         
    }
}
finally {
    resultSet.close();
}
```
So what we have done is use a StreamUDF to process the output of the secondary index query to filter on a specific Bin value.

Although a large set can be requested and filtered on the client, Aerospike allows creation of very arbitrary and flexible filters using User Defined Functions. This allows extension of the database to very arbitrary use cases.
You can do complex queries using Aerospike. They can be simple Primary Key operations, more complex secondary index Queries or very sophisticated Aggregation.


