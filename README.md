## Simulated DBMS

This project aims to simulate the functionality of a Database Management System in the block and record level. This is done by implementing an **_Extendible Hash Table_** data structure, which is one of the structures most commonly used in the real world to implement Database Management Systems. This was part of the _Database Systems Implementation_ course at the _Department of Informatics and Telecommunications_ in the _University of Athens_.

## Extendible Hashing

Extendible Hashing is a dynamic hashing method wherein directories, and buckets are used to hash data. It is an aggressively flexible method in which the hash function also experiences dynamic changes.

You can read more about how Extendible Hashing works (here)[https://en.wikipedia.org/wiki/Extendible_hashing] and also (here)[https://www.geeksforgeeks.org/extendible-hashing-dynamic-approach-to-dbms/]

## Block level

This project implements an Extendible Hash Table using the _Block Functions_ library, which can be used to access and manipulate memory at the block level. More specifically:

* The files contain records of type Record.
* The Block level Functions (with the prefix _BF__) are used to implement the functions that manage the Hash Table (with prefix _HT__).

This project explores many concepts both algorithmically (implementing multiple complicated functions such as splitting the buckets or doubling the hash table when needed using recursion) as well as many low level programming concepts (having a solid grasp on how memory is manipulated on the block level, pinning, dirtying and unpinning blocks etc).

Make: ```make ht```

Run: ```./build/runner```
