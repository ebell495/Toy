#include "literal_dictionary.h"

#include "memory.h"

#include "console_colors.h"

#include <stdio.h>

//util functions
static void setEntryValues(_entry* entry, Literal key, Literal value) {
	//free the original string/identifier and overwrite it
	if (IS_STRING(entry->key) || IS_IDENTIFIER(entry->key)) {
		freeLiteral(entry->key);
	}

	//take ownership of the copied string
	if (IS_STRING(key)) {
		entry->key = TO_STRING_LITERAL( copyString(AS_STRING(key), strlen(AS_STRING(key)) ), strlen(AS_STRING(key)));
	}

	//OR take ownership of the copied identifier
	else if (IS_IDENTIFIER(key)) {
		entry->key = TO_IDENTIFIER_LITERAL( copyString(AS_IDENTIFIER(key), strlen( AS_IDENTIFIER(key))), strlen(AS_IDENTIFIER(key)) );
	}

	else {
		freeLiteral(entry->key); //for types
		entry->key = key;
	}

	//values
	freeLiteral(entry->value);

	//take ownership of the copied string
	if (IS_STRING(value)) {
		char* buffer = ALLOCATE(char, strlen(AS_STRING(value)) + 1);
		strncpy(buffer, AS_STRING(value), strlen(AS_STRING(value)));
		buffer[strlen(AS_STRING(value))] = '\0';
		entry->value = TO_STRING_LITERAL(buffer, strlen(buffer));
	}

	//OR take ownership of the copied function
	else if (IS_FUNCTION(value)) {
		unsigned char* buffer = ALLOCATE(unsigned char, value.as.function.length);
		memcpy(buffer, AS_FUNCTION(value), value.as.function.length);

		entry->value = TO_FUNCTION_LITERAL(buffer, value.as.function.length);

		//save the scope
		entry->value.as.function.scope = value.as.function.scope;
	}

	else {
		entry->value = value;
	}
}

static _entry* getEntryArray(_entry* array, int capacity, Literal key, unsigned int hash, bool mustExist) {
	//find "key", starting at index
	unsigned int index = hash % capacity;
	unsigned int start = index;

	//increment once, so it can't equal start
	index = (index + 1) % capacity;

	//literal probing and collision checking
	while (index != start) { //WARNING: this is the only function allowed to retrieve an entry from the array
		_entry* entry = &array[index];

		if (IS_NULL(entry->key)) { //if key is empty, it's either empty or tombstone
			if (IS_NULL(entry->value) && !mustExist) {
				//found a truly empty bucket
				return entry;
			}
			//else it's a tombstone - ignore
		} else {
			if (literalsAreEqual(key, entry->key)) {
				return entry;
			}
		}

		index = (index + 1) % capacity;
	}

	return NULL;
}

static void adjustEntryCapacity(_entry** dictionaryHandle, int oldCapacity, int capacity) {
	//new entry space
	_entry* newEntries = ALLOCATE(_entry, capacity);

	for (int i = 0; i < capacity; i++) {
		newEntries[i].key = TO_NULL_LITERAL;
		newEntries[i].value = TO_NULL_LITERAL;
	}

	//move the old array into the new one
	for (int i = 0; i < oldCapacity; i++) {
		if (IS_NULL((*dictionaryHandle)[i].key)) {
			continue;
		}

		//place the key and value in the new array (reusing string memory)
		_entry* entry = getEntryArray(newEntries, capacity, TO_NULL_LITERAL, hashLiteral((*dictionaryHandle)[i].key), false);

		entry->key = (*dictionaryHandle)[i].key;
		entry->value = (*dictionaryHandle)[i].value;
	}

	//clear the old array
	FREE_ARRAY(_entry, *dictionaryHandle, oldCapacity);

	*dictionaryHandle = newEntries;
}

static bool setEntryArray(_entry** dictionaryHandle, int* capacityPtr, int contains, Literal key, Literal value, int hash) {
	//expand array if needed
	if (contains + 1 > *capacityPtr * DICTIONARY_MAX_LOAD) {
		int oldCapacity = *capacityPtr;
		*capacityPtr = GROW_CAPACITY(*capacityPtr);
		adjustEntryCapacity(dictionaryHandle, oldCapacity, *capacityPtr); //custom rather than automatic reallocation
	}

	_entry* entry = getEntryArray(*dictionaryHandle, *capacityPtr, key, hash, false);

	//if it's a string or an identifier, make a local copy
	if (IS_STRING(key)) {
		key = TO_STRING_LITERAL(copyString(AS_STRING(key), strlen(AS_STRING(key)) ), strlen(AS_STRING(key)));
	}
	if (IS_IDENTIFIER(key)) {
		key = TO_IDENTIFIER_LITERAL(copyString(AS_IDENTIFIER(key), strlen(AS_IDENTIFIER(key)) ), strlen(AS_IDENTIFIER(key)));
	}

	if (IS_STRING(value)) {
		key = TO_STRING_LITERAL(copyString(AS_STRING(value), strlen(AS_STRING(value)) ), strlen(AS_STRING(value)));
	}
	if (IS_IDENTIFIER(value)) {
		key = TO_IDENTIFIER_LITERAL(copyString(AS_IDENTIFIER(value), strlen(AS_IDENTIFIER(value)) ), strlen(AS_IDENTIFIER(value)));
	}

	//true = contains increase
	if (IS_NULL(entry->key)) {
		setEntryValues(entry, key, value);
		return true;
	}
	else {
		setEntryValues(entry, key, value);
		return false;
	}

	return false;
}

static void freeEntry(_entry* entry) {
	freeLiteral(entry->key);
	freeLiteral(entry->value);
	entry->key = TO_NULL_LITERAL;
	entry->value = TO_NULL_LITERAL;
}

static void freeEntryArray(_entry* array, int capacity) {
	if (array == NULL) {
		return;
	}

	for (int i = 0; i < capacity; i++) {
		if (!IS_NULL(array[i].key)) {
			freeEntry(&array[i]);
		}
	}

	FREE_ARRAY(_entry, array, capacity);
}

//exposed functions
void initLiteralDictionary(LiteralDictionary* dictionary) {
	//HACK: because modulo by 0 is undefined, set the capacity to a non-zero value (and allocate the arrays)
	dictionary->entries = NULL;
	dictionary->capacity = GROW_CAPACITY(0);
	dictionary->contains = 0;
	dictionary->count = 0;
	adjustEntryCapacity(&dictionary->entries, 0, dictionary->capacity);
}

void freeLiteralDictionary(LiteralDictionary* dictionary) {
	freeEntryArray(dictionary->entries, dictionary->capacity);
	dictionary->capacity = 0;
	dictionary->contains = 0;
}

void setLiteralDictionary(LiteralDictionary* dictionary, Literal key, Literal value) {
	if (IS_NULL(key)) {
		fprintf(stderr, ERROR "[internal] Dictionaries can't have null keys\n" RESET);
		return;
	}

	const int increment = setEntryArray(&dictionary->entries, &dictionary->capacity, dictionary->contains, key, value, hashLiteral(key));

	if (increment) {
		dictionary->contains++;
		dictionary->count++;
	}
}

Literal getLiteralDictionary(LiteralDictionary* dictionary, Literal key) {
	if (IS_NULL(key)) {
		fprintf(stderr, ERROR "[internal] Dictionaries can't have null keys\n" RESET);
		return TO_NULL_LITERAL;
	}

	_entry* entry = getEntryArray(dictionary->entries, dictionary->capacity, key, hashLiteral(key), true);

	if (entry != NULL) {
		return entry->value;
	}
	else {
		return TO_NULL_LITERAL;
	}
}

void removeLiteralDictionary(LiteralDictionary* dictionary, Literal key) {
	if (IS_NULL(key)) {
		fprintf(stderr, ERROR "[internal] Dictionaries can't have null keys\n" RESET);
		return;
	}

	_entry* entry = getEntryArray(dictionary->entries, dictionary->capacity, key, hashLiteral(key), true);

	if (entry != NULL) {
		freeEntry(entry);
		entry->value = TO_BOOLEAN_LITERAL(true); //tombstone
		dictionary->count--;
	}
}

bool existsLiteralDictionary(LiteralDictionary* dictionary, Literal key) {
	//null & not tombstoned
	_entry* entry = getEntryArray(dictionary->entries, dictionary->capacity, key, hashLiteral(key), false);
	return !(IS_NULL(entry->key) && IS_NULL(entry->value));
}