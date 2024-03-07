#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>

#include "bf.h"
#include "hash_file.h"

#define MAX_OPEN_FILES 20
#define NO_NEXT_BLOCK -1

// Global array that stores all the open files
HT_info* openfiles[MAX_OPEN_FILES];

#define CALL_BF(call)       \
{                           \
  BF_ErrorCode code = call; \
  if (code != BF_OK) {      \
    BF_PrintError(code);    \
    return HP_ERROR;        \
  }                         \
}

void printRecord(Record rec) {
	printf("{ID:%d, Name:%s, Surname:%s, City:%s}\n", rec.id, rec.name, rec.surname, rec.city);
}

// Function to reverse bits of num
unsigned int reverseBits(unsigned int num)
{
    unsigned int count = sizeof(num) * 8 - 1;
    unsigned int reverse_num = num;
 
    num >>= 1;
    while (num) {
        reverse_num <<= 1;
        reverse_num |= num & 1;
        num >>= 1;
        count--;
    }
    reverse_num <<= count;
    return reverse_num;
}

unsigned int hashInt(int value, int buckets) {
	unsigned int reversed = reverseBits(value);
	return reversed;
}

HT_ErrorCode printHT(int indexDesc) {
	if(openfiles[indexDesc] == NULL)
		return HT_ERROR;
	int ht_size = pow(2,openfiles[indexDesc]->depth);
	printf("Printing HT...\n");
	for(int i = 0; i < ht_size; i++)
	{
		printf("%d -> %d\n", i, openfiles[indexDesc]->HT[i]);
	}
	return HT_OK;
}

// Function for doubling the length of the Hash Table
// -> Allocates a new array double the length of the previous one
// -> Sets pointers to the corresponding buckets

HT_ErrorCode doubleHT(int indexDesc) {
	openfiles[indexDesc]->depth++;
	int newpointers = pow(2, openfiles[indexDesc]->depth);

	int* newarray = (int*)malloc(sizeof(int) * newpointers);

	if(openfiles[indexDesc]->HT == NULL)
		return HT_ERROR;
	
	for(int i = 0; i < newpointers; i++) {
		newarray[i] = openfiles[indexDesc]->HT[i / 2];
	}
	free(openfiles[indexDesc]->HT);
	openfiles[indexDesc]->HT = newarray;

	return HT_OK;
}

// -> Splits a bucket in two
// -> Allocates one extra bucket
// -> Separates buddies in these two buckets
// -> Updates local depth for both buckets 

HT_ErrorCode split(int indexDesc, Record record, int localDepth) {
	localDepth++;
	int bits = sizeof(int) * 8;
	int pointers = pow(2, openfiles[indexDesc]->depth);
	unsigned int hash = hashInt(record.id, pointers);
	unsigned int hashValue = (hash >> (bits - openfiles[indexDesc]->depth)) % pointers;
	BF_Block* block = NULL;
	BF_Block* new_bucket = NULL;
	char* new_bucket_data = NULL;
	char* data = NULL;
	int bucketID = openfiles[indexDesc]->HT[hashValue];
	int new_bucketID = 0;

  	BF_Block_Init(&block);
	if(BF_GetBlock(openfiles[indexDesc]->fd, bucketID, block) != BF_OK)
		return HT_ERROR;

  	data = BF_Block_GetData(block);

	// Initialize temporary array with the records from the bucket that is full
	// PLUS that one extra record that has to be inserted
	Record toBeRehashed[9];
	memset(&toBeRehashed, 0, sizeof(Record) * 9);
	for(int i = 0; i < 8; i++)
	{
		memcpy(&toBeRehashed[i], data + sizeof(Record) * i, sizeof(Record));
	}
	toBeRehashed[8] = record;

	// Decrement rec_count since it will be incremented in insert()
	openfiles[indexDesc]->rec_count = openfiles[indexDesc]->rec_count - openfiles[indexDesc]->rec_per_block;

	// Distinguish buddies in current array
	// End result: buddies in range <first> - <last>
	int first = 0;
	int last = 0;
	int i;
	for(i = 0; i < pointers; i++) {
		if(openfiles[indexDesc]->HT[i] == bucketID){
			first = i;
			break;
		}
	}
	while(i < pointers && openfiles[indexDesc]->HT[i] == bucketID)
		i++;
	last = i - 1;
	int half = (first + last) / 2 + 1;

	// Initialize new bucket and allocate
    BF_Block_Init(&new_bucket);
    if (BF_AllocateBlock(openfiles[indexDesc]->fd, new_bucket) != BF_OK) 
        return HT_ERROR;
		
	// Get new bucket ID
    BF_GetBlockCounter(openfiles[indexDesc]->fd, &new_bucketID);
    new_bucketID--;

	// Initialize new bucket data
    new_bucket_data = BF_Block_GetData(new_bucket);
    memset(new_bucket_data, 0, BF_BLOCK_SIZE);

	// Rearrange pointers
	for(i = half; i <= last; i++) {
		openfiles[indexDesc]->HT[i] = new_bucketID;
	}

	// Write local depth and record count in memory for both buckets
	memcpy(data + openfiles[indexDesc]->rec_per_block * sizeof(Record), &localDepth, sizeof(int));
	memset(data + sizeof(int) + openfiles[indexDesc]->rec_per_block * sizeof(Record), 0, sizeof(int));
	memcpy(new_bucket_data + openfiles[indexDesc]->rec_per_block * sizeof(Record), &localDepth, sizeof(int));
	memset(new_bucket_data + sizeof(int) + openfiles[indexDesc]->rec_per_block * sizeof(Record), 0, sizeof(int));
	
	// Set blocks as dirty, unpin them and destroy pointers
	BF_Block_SetDirty(block);
  	if(BF_UnpinBlock(block) != BF_OK)
		return HT_ERROR;

	BF_Block_Destroy(&block);

	BF_Block_SetDirty(new_bucket);
	if(BF_UnpinBlock(new_bucket) != BF_OK)
		return HT_ERROR;

	BF_Block_Destroy(&new_bucket);

	// Insert every record from temporary array in HT
	for(int i = 0 ; i < openfiles[indexDesc]->rec_per_block + 1; i++) {
    	HT_InsertEntry(indexDesc, toBeRehashed[i]);
  	}

	return HT_OK;
}

HT_ErrorCode HT_Init() {
	// Initialize array of open file pointers to NULL
	for(int i = 0 ; i < MAX_OPEN_FILES ; i++)
		openfiles[i] = NULL;

	return HT_OK;
}

HT_ErrorCode HT_CreateIndex(const char *filename, int depth) {
	BF_Block* metadata_block = NULL;
	BF_Block* first_bucket = NULL;
	int fd = 0;
	int first_bucket_id = 0;
	HT_info htInfo;

	// Create and open file <filename>
	if (BF_CreateFile(filename) != BF_OK)
		return HT_ERROR;
	
	if (BF_OpenFile(filename, &fd) != BF_OK) 
		return HT_ERROR;

	// Initialize BF_Block struct, allocate and initialize metadata block
	BF_Block_Init(&metadata_block);
	if (BF_AllocateBlock(fd, metadata_block) != BF_OK) 
		return HT_ERROR;

	// Load information into HT_info struct for metadata block
	memset(&htInfo, 0, sizeof(HT_info));
	htInfo.fd = fd;
	htInfo.depth = depth;

	// Records per block = (BLOCK SIZE - one int to count records in block - one int for local depth)/record size
	htInfo.rec_per_block = (BF_BLOCK_SIZE - 2 * sizeof(int))/sizeof(Record);

	// Capacity of HT Blocks = (BLOCK SIZE - one int to point to the next block)/INT SIZE
	// HT Blocks hold block IDs of the blocks acting as the hash table buckets
	// as well as the block ID of the next HT Block
	htInfo.HT_block_capacity = (BF_BLOCK_SIZE - sizeof(int))/sizeof(int);

	// Create first bucket

	BF_Block_Init(&first_bucket);
	if (BF_AllocateBlock(fd, first_bucket) != BF_OK) 
		return HT_ERROR;

	// Initialize first bucket's local depth and record counter
	char* first_bucket_data = BF_Block_GetData(first_bucket);
	memset(first_bucket_data, 0, BF_BLOCK_SIZE);

	//Getting first bucket's id so all the hash table's block ids can point to it
	BF_GetBlockCounter(fd, &first_bucket_id);
	first_bucket_id--;


	// Calculate amount of blocks needed for Hash Table
	int ht_size = pow(2, htInfo.depth);

	htInfo.HT = malloc(sizeof(int) * ht_size);
	for(int i = 0; i < ht_size; i++) {
		htInfo.HT[i] = first_bucket_id;
	}

	// Write metadata in memory
	char* data = BF_Block_GetData(metadata_block);
	memcpy(data, &htInfo, sizeof(HT_info));


	// Set blocks as dirty, unpin them & destroy bf block struct
	BF_Block_SetDirty(metadata_block);
	BF_Block_SetDirty(first_bucket);
	if(BF_UnpinBlock(metadata_block) != BF_OK)
		return HT_ERROR;
	if(BF_UnpinBlock(first_bucket) != BF_OK)
		return HT_ERROR;

	BF_Block_Destroy(&metadata_block);
	BF_Block_Destroy(&first_bucket);

	if(BF_CloseFile(fd) != BF_OK)
		return HT_ERROR;

	return HT_OK;
}

HT_ErrorCode HT_OpenIndex(const char *fileName, int *indexDesc){
	BF_Block* block = NULL;
	HT_info* htInfo = NULL;
	int fd = 0;
	bool file_Was_Opened = false;

	if(BF_OpenFile(fileName, &fd) != BF_OK)
		return HT_ERROR;

	// Initialize BF_Block struct and get metadata block
	BF_Block_Init(&block);
	if(BF_GetBlock(fd, 0, block) != BF_OK)
		return HT_ERROR;
	
	// Search for an open slot in open files array
	for(int i = 0; i < MAX_OPEN_FILES; i++)
	{
		// If a slot is open
		if(openfiles[i] == NULL)
		{
			// -> Allocate memory for open slot in open files array
			// -> Copy HT_info from metadata block to the open slot of open files array
			// -> Update indexDesc
			openfiles[i] = (HT_info *)malloc(sizeof(HT_info));
			htInfo = (HT_info *)BF_Block_GetData(block);
			memcpy(openfiles[i], htInfo, sizeof(HT_info));
			*indexDesc = i;
			file_Was_Opened = true;
			break;
		}
	}

	// Unpin metadata block and destroy BF_Block struct
	if(BF_UnpinBlock(block) != BF_OK)
		return HT_ERROR;
	BF_Block_Destroy(&block);

	// If every slot in open files array is taken, return error code
	if(!(file_Was_Opened)) {
		printf("Open file limit reached, file was not opened");
		return HT_ERROR;
	}
		
	return HT_OK;
}

HT_ErrorCode HT_CloseFile(int indexDesc) {
	BF_Block* block = NULL;
	HT_info* htInfo = NULL;

	// Get metadata block
	BF_Block_Init(&block);
	if(BF_GetBlock(openfiles[indexDesc]->fd, 0, block) != BF_OK)
		return HT_ERROR;

	// Update metadata (in case there was a change)
	htInfo = (HT_info*)BF_Block_GetData(block);
	memcpy(htInfo, openfiles[indexDesc], sizeof(HT_info));

	// Set metadata block as dirty, unpin it & destroy BF_Block struct
	BF_Block_SetDirty(block);
	if(BF_UnpinBlock(block) != BF_OK)
		return HT_ERROR;
	BF_Block_Destroy(&block);

	// Close file
	if(BF_CloseFile(openfiles[indexDesc]->fd) != BF_OK)
		return HT_ERROR;

	// Free memory and set open file array slot to NULL
	// Also free Hash Table
	free(openfiles[indexDesc]->HT);
	free(openfiles[indexDesc]);
	openfiles[indexDesc] = NULL;

	return HT_OK;
}

HT_ErrorCode HT_InsertEntry(int indexDesc, Record record) {
	BF_Block* bucket_to_insert = NULL;
	char* data = NULL;
	int bucketID = 0;		

	// Calculate total pointers of Hash Table
	int pointers = pow(2, openfiles[indexDesc]->depth);

	// Calculate total bits of an integer:
	// sizeof(int) return number of bytes so
	// we multiply by 8 to get the number of bits
	int bits = sizeof(int) * 8;

	// Calculate 'total depth' Most Significant Bits (MSB)
	// and store them in hashValue
	unsigned int hash = hashInt(record.id, pointers);
	unsigned int hashValue = (hash >> (bits - openfiles[indexDesc]->depth)) % pointers;

	int recordcount = 0;
	int localDepth = 0;
	bucketID = openfiles[indexDesc]->HT[hashValue];

	BF_Block_Init(&bucket_to_insert);
	if(BF_GetBlock(openfiles[indexDesc]->fd, bucketID, bucket_to_insert) != BF_OK)
		return HT_ERROR;
	
	// Get Local depth and record count from bucket
	data = BF_Block_GetData(bucket_to_insert);
	memcpy(&localDepth, data + sizeof(Record) * openfiles[indexDesc]->rec_per_block, sizeof(int));
	memcpy(&recordcount, data + sizeof(int) + sizeof(Record) * openfiles[indexDesc]->rec_per_block, sizeof(int));

	// If there is space in current bucket
	if(recordcount < openfiles[indexDesc]->rec_per_block) {
		memcpy(data + sizeof(Record) * recordcount, &record, sizeof(Record));
		recordcount++;
		memcpy(data + sizeof(int) + sizeof(Record) * openfiles[indexDesc]->rec_per_block, &recordcount, sizeof(int));
	}else{
		// Check if we need to double the Hash Table
		if(localDepth == openfiles[indexDesc]->depth) {
			
			doubleHT(indexDesc);
			printHT(indexDesc);
		}
		// Check if we need to split current bucket
		if(localDepth < openfiles[indexDesc]->depth) {
			// split
			split(indexDesc, record, localDepth);
		}

	}

	// Increment record counter 
	openfiles[indexDesc]->rec_count++;

	// Set current block as dirty, unpin it and destroy bf block struct
	BF_Block_SetDirty(bucket_to_insert);
	if(BF_UnpinBlock(bucket_to_insert) != BF_OK)
		return HT_ERROR;

	BF_Block_Destroy(&bucket_to_insert);

	return HT_OK;
}

HT_ErrorCode HT_PrintAllEntries(int indexDesc, int *id) {
	BF_Block * bucket = NULL;
	BF_Block_Init(&bucket);
	char* data = NULL;
	int record_count = 0;
	Record temp_record;
	memset(&temp_record, 0, sizeof(Record));

    // Calculate total pointers of Hash Table
    int pointers = pow(2, openfiles[indexDesc]->depth);

	if(id != NULL) {
		// Calculate total bits of an integer:
		// sizeof(int) return number of bytes so
		// we multiply by 8 to get the number of bits
		int bits = sizeof(int) * 8;

		// Calculate 'total depth' Most Significant Bits (MSB)
		// and store them in hashValue
		unsigned int hash = hashInt(*id, pointers);
		unsigned int hashValue = (hash >> (bits - openfiles[indexDesc]->depth)) % pointers;

		// Get bucket and its data
		if(BF_GetBlock(indexDesc, openfiles[indexDesc]->HT[hashValue], bucket) != BF_OK)
       	 	return HT_ERROR;
		data = BF_Block_GetData(bucket);

		// Get record count from bucket
		memcpy(&record_count, data + sizeof(int) + openfiles[indexDesc]->rec_per_block * sizeof(Record), sizeof(int));
		for(int i = 0; i < record_count; i++ ) {
			// Get record and check if its id matches the one we are searching for
			memcpy(&temp_record, data + sizeof(Record) * i, sizeof(Record));
			if(temp_record.id == *id)
				printRecord(temp_record);
		}
		if(BF_UnpinBlock(bucket) != BF_OK)
			return HT_ERROR;
	} else {
		// For every Hash Table pointer
		for(int i = 0; i < pointers; i++) {
			// Get its bucket
			int blockID = openfiles[indexDesc]->HT[i];
			if(BF_GetBlock(indexDesc, blockID, bucket) != BF_OK)
       	 		return HT_ERROR;
			data = BF_Block_GetData(bucket);

			// Get record count from bucket
			memcpy(&record_count, data + sizeof(int) + openfiles[indexDesc]->rec_per_block * sizeof(Record), sizeof(int));
			for(int i = 0; i < record_count; i++ ){
				// Print every record in bucket
				memcpy(&temp_record, data + sizeof(Record) * i, sizeof(Record));
				printRecord(temp_record);
			}
			if(BF_UnpinBlock(bucket) != BF_OK)
				return HT_ERROR;
		}
	}
	// Destroy BF_Block Struct
	BF_Block_Destroy(&bucket);

  	return HT_OK;
}


// Opens file <filename>
// Prints: 
// a) total number of blocks allocated
// b) minimum record count in a bucket
// c) average record count in a bucket
// d) maximum record count in a bucket

HT_ErrorCode HashStatistics(char* filename)
{	
	BF_Block* block = NULL;
	BF_Block* bucket = NULL;
	char* data = NULL;
	HT_info* htinfo = NULL;
	int indexDesc;
	int recordcount = 0;

	BF_OpenFile(filename, &indexDesc);

	BF_Block_Init(&block);
	if(BF_GetBlock(indexDesc, 0, block) != BF_OK)
		return HT_ERROR;

	htinfo = (HT_info*)BF_Block_GetData(block);

	int records = 0;
	int max = -1;
	int min = htinfo->rec_per_block + 1;
	int buckets = 0; 
	BF_GetBlockCounter(indexDesc, &buckets);
	buckets--;

	BF_Block_Init(&bucket);
	// For every bucket
	for(int i = 1; i < buckets; i++) {
		
		if(BF_GetBlock(htinfo->fd, i, bucket) != BF_OK)
			return HT_ERROR;
		
		// Get its record count
		data = BF_Block_GetData(bucket);
		memcpy(&recordcount, data + htinfo->rec_per_block * sizeof(Record) + sizeof(int), sizeof(int));
		if(recordcount < min)
			min = recordcount;
		if(recordcount > max)
			max = recordcount;
		records += recordcount;

		if(BF_UnpinBlock(bucket) != BF_OK)
			return HT_ERROR;
		
	}
	BF_Block_Destroy(&bucket);

	printf("Now printing statistics for file <%s>...\n", filename);
	printf("Total number of blocks: %d\n", buckets + 1);
	printf("Minimum record count in a bucket: %d\n", min);
	printf("Average record count in a bucket: ");
	if(buckets == 0) {
		printf("Could not be defined (0 buckets created)\n");
	} else {
		printf("%f\n", (float) records / buckets);
	}
	printf("Maximum record count in a bucket: %d\n", max);

	BF_Block_SetDirty(block);
	if(BF_UnpinBlock(block) != BF_OK)
		return HT_ERROR;
	BF_Block_Destroy(&block);

	BF_CloseFile(indexDesc);

	printf("Finished printing statistics\n");

	return HT_OK;
}
