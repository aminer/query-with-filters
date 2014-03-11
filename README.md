#Query with multiple filters
This example demonstrates how to query on a secondary index and apply additional predicates or filters 


##Problem
Aerospike supports specification of a secondary index to restrict the set of rows that will be acted on. Instead of a query optimizer which could make the wrong choices, the developer must choose an initial index.

Although a large set can be requested and filtered on the client, Aerospike allows creation of very arbitrary and flexible filters using User Defined Functions (UDFs). This allows extension of the database to very arbitrary use cases.

##Solution
Aerospike has an [Aggregation](https://docs.aerospike.com/display/V3/Aggregation+Guide) feature that can be used to address this use. Aggregations operate on, or process,  a stream using StreamUDFs. A stream is the output of a query on a secondary index. Imagine a stream of records flowing out of a query and passing through one or more StreamUDFs, and the result set being passed back to your application. See Aggregation Guide for more detailed information.  

##Scenario
You want to authenticate a user. You have user profiles in a Set “profile” and each user profile record has Bins: “username” and “password”. 

Create a secondary index on “username”. An easy way to do this is to use AQL. Run AQL on a Linux machine and enter the following command:
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
select * from test.profile where username = 'mary'
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
Both the AQL and the java code are semantically equivelent. They perform a simple secondary index query on “username”, but do not filter on “password”. The result is the record(s) with the matching username. 

This is the first part of the solution.

###StreamUDF
The next step is to add a filter on “password”. To do this you will need to use [Aggregations](https://docs.aerospike.com/display/V3/Aggregation+Guide) and write a [StreamUDF](https://docs.aerospike.com/pages/viewpage.action?pageId=3807962) in Lua.

StreamUDF can be a little baffeling, but not really, consider this diagram:

![Stream processing](query_stream_filter.png)

The output of the query is a stream of records. This stream can be processed by zero or more of filters. As the Query runs on all nodes in the cluster, so does the filter function. 

The Map function is a mechanism to collect elements from the stream to be returned, in the following code you will see that the ```map_profile``` function returns the "name" Bin value and the "password" Bin value.

Finally you can supply a reduce function that can aggregate values from the map function(s). The reduct function runs on each node in the cluster and the final reduce is done on the client where it aggregates the results from each node.

Lets take a look at the complete [StreamUDF](https://docs.aerospike.com/pages/viewpage.action?pageId=3807962) written in Lua.
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
The function ```check_password``` configures how the aggregation will be applied to the stream. You can see that the first parameter to this function is the stream. Look at the return statement 
```lua
return stream : filter(filter_password) : map(map_profile)
```
This line configures the function ```filter_password``` as a filter function, and applies ```filter_password``` to the stream. Next it configures the function ```map_profile``` as a map function and applies it to the stream after the filter.

The ```check_password``` function is applied to the stream as part of an aggregation query. 

###Register you StreamUDF
Before you can use the StreamUDF, you must refister it. The above code is stored in a file named 'profile.lua'. To register this Lua package as a UDF, execute the following aql:
```
register module 'udf/profile.lua'
```
The package is installed on all nodes in the Aerospike cluster.

##Running the query with the filter
Here is how you run the aggregation query in AQL:
```
aggregate profile.check_password('ghjks') on test.profile where username = 'Mary'
```
and the same in Java:
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

##The Code
The code for this example is in GitHub [https://github.com/aerospike/query-with-filters].

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

##Conclusion
You can do complex queries using Aerospike. They can be simple Primary Key operations, more complex secondary index Queries or very sophisticated Aggregation queries.


