This project implements an Extendible Hash Table using the Block Functions library, which can be used to access and manipulate memory at the block level. This was part of the Database Systems Implementation course at the Department of Informatics and Telecommunications in the University of Athens.

Extendible Hashing is a dynamic hashing method wherein directories, and buckets are used to hash data. It is an aggressively flexible method in which the hash function also experiences dynamic changes.

You can read more about how Extendible Hashing works here: https://en.wikipedia.org/wiki/Extendible_hashing

This project explores many concepts both algorithmically (implementing multiple complicated functions such as splitting the buckets or doubling the hash table when needed using recursion) as well as many low level programming concepts (having a solid grasp on how memory is manipulated on the block level, pinning, dirtying and unpinning blocks etc).

Make: make ht

Run: ./build/runner
