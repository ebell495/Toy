#include "lib_runner.h"

#include "toy_memory.h"
#include "toy_drive_system.h"
#include "toy_interpreter.h"

#include "repl_tools.h"

#include <stdlib.h>

typedef struct Toy_Runner {
	Toy_Interpreter interpreter;
	const unsigned char* bytecode;
	size_t size;

	bool dirty;
} Toy_Runner;

//Toy native functions
static int nativeLoadScript(Toy_Interpreter* interpreter, Toy_LiteralArray* arguments) {
	//arguments
	if (arguments->count != 1) {
		interpreter->errorOutput("Incorrect number of arguments to loadScript\n");
		return -1;
	}

	//get the file path literal with a handle
	Toy_Literal drivePathLiteral = Toy_popLiteralArray(arguments);

	Toy_Literal drivePathLiteralIdn = drivePathLiteral;
	if (TOY_IS_IDENTIFIER(drivePathLiteral) && Toy_parseIdentifierToValue(interpreter, &drivePathLiteral)) {
		Toy_freeLiteral(drivePathLiteralIdn);
	}

	if (TOY_IS_IDENTIFIER(drivePathLiteral)) {
		Toy_freeLiteral(drivePathLiteral);
		return -1;
	}

	Toy_Literal filePathLiteral = Toy_getDrivePathLiteral(interpreter, &drivePathLiteral);

	if (TOY_IS_NULL(filePathLiteral)) {
		Toy_freeLiteral(filePathLiteral);
		Toy_freeLiteral(drivePathLiteral);
		return -1;
	}

	Toy_freeLiteral(drivePathLiteral);

	//use raw types - easier
	const char* filePath = Toy_toCString(TOY_AS_STRING(filePathLiteral));
	size_t filePathLength = Toy_lengthRefString(TOY_AS_STRING(filePathLiteral));

	//load and compile the bytecode
	size_t fileSize = 0;
	const char* source = (const char*)Toy_readFile(filePath, &fileSize);

	if (!source) {
		interpreter->errorOutput("Failed to load source file\n");
		Toy_freeLiteral(filePathLiteral);
		return -1;
	}

	const unsigned char* bytecode = Toy_compileString(source, &fileSize);
	free((void*)source);

	if (!bytecode) {
		interpreter->errorOutput("Failed to compile source file\n");
		Toy_freeLiteral(filePathLiteral);
		return -1;
	}

	//build the runner object
	Toy_Runner* runner = TOY_ALLOCATE(Toy_Runner, 1);
	Toy_setInterpreterPrint(&runner->interpreter, interpreter->printOutput);
	Toy_setInterpreterAssert(&runner->interpreter, interpreter->assertOutput);
	Toy_setInterpreterError(&runner->interpreter, interpreter->errorOutput);
	runner->interpreter.hooks = interpreter->hooks;
	runner->interpreter.scope = NULL;
	Toy_resetInterpreter(&runner->interpreter);
	runner->bytecode = bytecode;
	runner->size = fileSize;
	runner->dirty = false;

	//build the opaque object, and push it to the stack
	Toy_Literal runnerLiteral = TOY_TO_OPAQUE_LITERAL(runner, TOY_OPAQUE_TAG_RUNNER);
	Toy_pushLiteralArray(&interpreter->stack, runnerLiteral);

	//free the drive path
	Toy_freeLiteral(filePathLiteral);

	return 1;
}

static int nativeLoadScriptBytecode(Toy_Interpreter* interpreter, Toy_LiteralArray* arguments) {
	//arguments
	if (arguments->count != 1) {
		interpreter->errorOutput("Incorrect number of arguments to loadScriptBytecode\n");
		return -1;
	}

	//get the argument
	Toy_Literal drivePathLiteral = Toy_popLiteralArray(arguments);

	Toy_Literal drivePathLiteralIdn = drivePathLiteral;
	if (TOY_IS_IDENTIFIER(drivePathLiteral) && Toy_parseIdentifierToValue(interpreter, &drivePathLiteral)) {
		Toy_freeLiteral(drivePathLiteralIdn);
	}

	if (TOY_IS_IDENTIFIER(drivePathLiteral)) {
		Toy_freeLiteral(drivePathLiteral);
		return -1;
	}

	Toy_Literal filePathLiteral = Toy_getDrivePathLiteral(interpreter, &drivePathLiteral);

	if (TOY_IS_NULL(filePathLiteral)) {
		Toy_freeLiteral(filePathLiteral);
		Toy_freeLiteral(drivePathLiteral);
		return -1;
	}

	Toy_freeLiteral(drivePathLiteral);

	//use raw types - easier
	const char* filePath = Toy_toCString(TOY_AS_STRING(filePathLiteral));
	size_t filePathLength = Toy_lengthRefString(TOY_AS_STRING(filePathLiteral));

	//load the bytecode
	size_t fileSize = 0;
	unsigned char* bytecode = (unsigned char*)Toy_readFile(filePath, &fileSize);

	if (!bytecode) {
		interpreter->errorOutput("Failed to load bytecode file\n");
		return -1;
	}

	//build the runner object
	Toy_Runner* runner = TOY_ALLOCATE(Toy_Runner, 1);
	Toy_setInterpreterPrint(&runner->interpreter, interpreter->printOutput);
	Toy_setInterpreterAssert(&runner->interpreter, interpreter->assertOutput);
	Toy_setInterpreterError(&runner->interpreter, interpreter->errorOutput);
	runner->interpreter.hooks = interpreter->hooks;
	runner->interpreter.scope = NULL;
	Toy_resetInterpreter(&runner->interpreter);
	runner->bytecode = bytecode;
	runner->size = fileSize;
	runner->dirty = false;

	//build the opaque object, and push it to the stack
	Toy_Literal runnerLiteral = TOY_TO_OPAQUE_LITERAL(runner, TOY_OPAQUE_TAG_RUNNER);
	Toy_pushLiteralArray(&interpreter->stack, runnerLiteral);

	//free the drive path
	Toy_freeLiteral(filePathLiteral);

	return 1;
}

static int nativeRunScript(Toy_Interpreter* interpreter, Toy_LiteralArray* arguments) {
	//no arguments
	if (arguments->count != 1) {
		interpreter->errorOutput("Incorrect number of arguments to runScript\n");
		return -1;
	}

	//get the runner object
	Toy_Literal runnerLiteral = Toy_popLiteralArray(arguments);

	Toy_Literal runnerIdn = runnerLiteral;
	if (TOY_IS_IDENTIFIER(runnerLiteral) && Toy_parseIdentifierToValue(interpreter, &runnerLiteral)) {
		Toy_freeLiteral(runnerIdn);
	}

	if (TOY_IS_IDENTIFIER(runnerLiteral)) {
		Toy_freeLiteral(runnerLiteral);
		return -1;
	}

	if (TOY_GET_OPAQUE_TAG(runnerLiteral) != TOY_OPAQUE_TAG_RUNNER) {
		interpreter->errorOutput("Unrecognized opaque literal in runScript\n");
		return -1;
	}

	Toy_Runner* runner = TOY_AS_OPAQUE(runnerLiteral);

	//run
	if (runner->dirty) {
		interpreter->errorOutput("Can't re-run a dirty script (try resetting it first)\n");
		Toy_freeLiteral(runnerLiteral);
		return -1;
	}

	unsigned char* bytecodeCopy = TOY_ALLOCATE(unsigned char, runner->size);
	memcpy(bytecodeCopy, runner->bytecode, runner->size); //need a COPY of the bytecode, because the interpreter eats it

	Toy_runInterpreter(&runner->interpreter, bytecodeCopy, runner->size);
	runner->dirty = true;

	//cleanup
	Toy_freeLiteral(runnerLiteral);

	return 0;
}

static int nativeGetScriptVar(Toy_Interpreter* interpreter, Toy_LiteralArray* arguments) {
	//no arguments
	if (arguments->count != 2) {
		interpreter->errorOutput("Incorrect number of arguments to getScriptVar\n");
		return -1;
	}

	//get the runner object
	Toy_Literal varName = Toy_popLiteralArray(arguments);
	Toy_Literal runnerLiteral = Toy_popLiteralArray(arguments);

	Toy_Literal varNameIdn = varName;
	if (TOY_IS_IDENTIFIER(varName) && Toy_parseIdentifierToValue(interpreter, &varName)) {
		Toy_freeLiteral(varNameIdn);
	}

	Toy_Literal runnerIdn = runnerLiteral;
	if (TOY_IS_IDENTIFIER(runnerLiteral) && Toy_parseIdentifierToValue(interpreter, &runnerLiteral)) {
		Toy_freeLiteral(runnerIdn);
	}

	if (TOY_IS_IDENTIFIER(varName) || TOY_IS_IDENTIFIER(runnerLiteral)) {
		Toy_freeLiteral(varName);
		Toy_freeLiteral(runnerLiteral);
		return -1;
	}

	if (TOY_GET_OPAQUE_TAG(runnerLiteral) != TOY_OPAQUE_TAG_RUNNER) {
		interpreter->errorOutput("Unrecognized opaque literal in getScriptVar\n");
		return -1;
	}

	Toy_Runner* runner = TOY_AS_OPAQUE(runnerLiteral);

	//dirty check
	if (!runner->dirty) {
		interpreter->errorOutput("Can't access variable from a non-dirty script (try running it first)\n");
		Toy_freeLiteral(runnerLiteral);
		return -1;
	}

	//get the desired variable
	Toy_Literal varIdn = TOY_TO_IDENTIFIER_LITERAL(Toy_copyRefString(TOY_AS_STRING(varName)));
	Toy_Literal result = TOY_TO_NULL_LITERAL;
	Toy_getScopeVariable(runner->interpreter.scope, varIdn, &result);

	Toy_pushLiteralArray(&interpreter->stack, result);

	//cleanup
	Toy_freeLiteral(result);
	Toy_freeLiteral(varIdn);
	Toy_freeLiteral(varName);
	Toy_freeLiteral(runnerLiteral);

	return 1;
}

static int nativeCallScriptFn(Toy_Interpreter* interpreter, Toy_LiteralArray* arguments) {
	//no arguments
	if (arguments->count < 2) {
		interpreter->errorOutput("Incorrect number of arguments to callScriptFn\n");
		return -1;
	}

	//get the rest args
	Toy_LiteralArray tmp;
	Toy_initLiteralArray(&tmp);

	while (arguments->count > 2) {
		Toy_Literal lit = Toy_popLiteralArray(arguments);
		Toy_pushLiteralArray(&tmp, lit);
		Toy_freeLiteral(lit);
	}

	Toy_LiteralArray rest;
	Toy_initLiteralArray(&rest);

	while (tmp.count > 0) { //correct the order of the rest args
		Toy_Literal lit = Toy_popLiteralArray(&tmp);
		Toy_pushLiteralArray(&rest, lit);
		Toy_freeLiteral(lit);
	}

	Toy_freeLiteralArray(&tmp);

	//get the runner object
	Toy_Literal varName = Toy_popLiteralArray(arguments);
	Toy_Literal runnerLiteral = Toy_popLiteralArray(arguments);

	Toy_Literal varNameIdn = varName;
	if (TOY_IS_IDENTIFIER(varName) && Toy_parseIdentifierToValue(interpreter, &varName)) {
		Toy_freeLiteral(varNameIdn);
	}

	Toy_Literal runnerIdn = runnerLiteral;
	if (TOY_IS_IDENTIFIER(runnerLiteral) && Toy_parseIdentifierToValue(interpreter, &runnerLiteral)) {
		Toy_freeLiteral(runnerIdn);
	}

	if (TOY_IS_IDENTIFIER(varName) || TOY_IS_IDENTIFIER(runnerLiteral)) {
		Toy_freeLiteral(varName);
		Toy_freeLiteral(runnerLiteral);
		return -1;
	}

	if (TOY_GET_OPAQUE_TAG(runnerLiteral) != TOY_OPAQUE_TAG_RUNNER) {
		interpreter->errorOutput("Unrecognized opaque literal in callScriptFn\n");
		return -1;
	}

	Toy_Runner* runner = TOY_AS_OPAQUE(runnerLiteral);

	//dirty check
	if (!runner->dirty) {
		interpreter->errorOutput("Can't access fn from a non-dirty script (try running it first)\n");
		Toy_freeLiteral(runnerLiteral);
		Toy_freeLiteralArray(&rest);
		return -1;
	}

	//get the desired variable
	Toy_Literal varIdn = TOY_TO_IDENTIFIER_LITERAL(Toy_copyRefString(TOY_AS_STRING(varName)));
	Toy_Literal fn = TOY_TO_NULL_LITERAL;
	Toy_getScopeVariable(runner->interpreter.scope, varIdn, &fn);

	if (!TOY_IS_FUNCTION(fn)) {
		interpreter->errorOutput("Can't run a non-function literal\n");
		Toy_freeLiteral(fn);
		Toy_freeLiteral(varIdn);
		Toy_freeLiteral(varName);
		Toy_freeLiteral(runnerLiteral);
		Toy_freeLiteralArray(&rest);
	}

	//call
	Toy_LiteralArray resultArray;
	Toy_initLiteralArray(&resultArray);

	Toy_callLiteralFn(interpreter, fn, &rest, &resultArray);

	Toy_Literal result = TOY_TO_NULL_LITERAL;
	if (resultArray.count > 0) {
		result = Toy_popLiteralArray(&resultArray);
	}

	Toy_pushLiteralArray(&interpreter->stack, result);

	//cleanup
	Toy_freeLiteralArray(&resultArray);
	Toy_freeLiteral(result);
	Toy_freeLiteral(fn);
	Toy_freeLiteral(varIdn);
	Toy_freeLiteral(varName);
	Toy_freeLiteral(runnerLiteral);
	Toy_freeLiteralArray(&rest);

	return 1;
}

static int nativeResetScript(Toy_Interpreter* interpreter, Toy_LiteralArray* arguments) {
	//no arguments
	if (arguments->count != 1) {
		interpreter->errorOutput("Incorrect number of arguments to resetScript\n");
		return -1;
	}

	//get the runner object
	Toy_Literal runnerLiteral = Toy_popLiteralArray(arguments);

	Toy_Literal runnerIdn = runnerLiteral;
	if (TOY_IS_IDENTIFIER(runnerLiteral) && Toy_parseIdentifierToValue(interpreter, &runnerLiteral)) {
		Toy_freeLiteral(runnerIdn);
	}

	if (TOY_IS_IDENTIFIER(runnerLiteral)) {
		Toy_freeLiteral(runnerLiteral);
		return -1;
	}

	if (TOY_GET_OPAQUE_TAG(runnerLiteral) != TOY_OPAQUE_TAG_RUNNER) {
		interpreter->errorOutput("Unrecognized opaque literal in resetScript\n");
		return -1;
	}

	Toy_Runner* runner = TOY_AS_OPAQUE(runnerLiteral);

	//reset
	if (!runner->dirty) {
		interpreter->errorOutput("Can't reset a non-dirty script (try running it first)\n");
		Toy_freeLiteral(runnerLiteral);
		return -1;
	}

	Toy_resetInterpreter(&runner->interpreter);
	runner->dirty = false;
	Toy_freeLiteral(runnerLiteral);

	return 0;
}

static int nativeFreeScript(Toy_Interpreter* interpreter, Toy_LiteralArray* arguments) {
	//no arguments
	if (arguments->count != 1) {
		interpreter->errorOutput("Incorrect number of arguments to freeScript\n");
		return -1;
	}

	//get the runner object
	Toy_Literal runnerLiteral = Toy_popLiteralArray(arguments);

	Toy_Literal runnerIdn = runnerLiteral;
	if (TOY_IS_IDENTIFIER(runnerLiteral) && Toy_parseIdentifierToValue(interpreter, &runnerLiteral)) {
		Toy_freeLiteral(runnerIdn);
	}

	if (TOY_IS_IDENTIFIER(runnerLiteral)) {
		Toy_freeLiteral(runnerLiteral);
		return -1;
	}

	if (TOY_GET_OPAQUE_TAG(runnerLiteral) != TOY_OPAQUE_TAG_RUNNER) {
		interpreter->errorOutput("Unrecognized opaque literal in freeScript\n");
		return -1;
	}

	Toy_Runner* runner = TOY_AS_OPAQUE(runnerLiteral);

	//clear out the runner object
	runner->interpreter.hooks = NULL;
	Toy_freeInterpreter(&runner->interpreter);
	TOY_FREE_ARRAY(unsigned char, runner->bytecode, runner->size);

	TOY_FREE(Toy_Runner, runner);

	Toy_freeLiteral(runnerLiteral);

	return 0;
}

static int nativeCheckScriptDirty(Toy_Interpreter* interpreter, Toy_LiteralArray* arguments) {
	//no arguments
	if (arguments->count != 1) {
		interpreter->errorOutput("Incorrect number of arguments to checkScriptDirty\n");
		return -1;
	}

	//get the runner object
	Toy_Literal runnerLiteral = Toy_popLiteralArray(arguments);

	Toy_Literal runnerIdn = runnerLiteral;
	if (TOY_IS_IDENTIFIER(runnerLiteral) && Toy_parseIdentifierToValue(interpreter, &runnerLiteral)) {
		Toy_freeLiteral(runnerIdn);
	}

	if (TOY_IS_IDENTIFIER(runnerLiteral)) {
		Toy_freeLiteral(runnerLiteral);
		return -1;
	}

	if (TOY_GET_OPAQUE_TAG(runnerLiteral) != TOY_OPAQUE_TAG_RUNNER) {
		interpreter->errorOutput("Unrecognized opaque literal in checkScriptDirty\n");
		return -1;
	}

	Toy_Runner* runner = TOY_AS_OPAQUE(runnerLiteral);

	//run
	Toy_Literal result = TOY_TO_BOOLEAN_LITERAL(runner->dirty);

	Toy_pushLiteralArray(&interpreter->stack, result);

	//cleanup
	Toy_freeLiteral(result);
	Toy_freeLiteral(runnerLiteral);

	return 0;
}

//call the hook
typedef struct Natives {
	const char* name;
	Toy_NativeFn fn;
} Natives;

int Toy_hookRunner(Toy_Interpreter* interpreter, Toy_Literal identifier, Toy_Literal alias) {
	//build the natives list
	Natives natives[] = {
		{"loadScript", nativeLoadScript},
		{"loadScriptBytecode", nativeLoadScriptBytecode},
		{"runScript", nativeRunScript},
		{"getScriptVar", nativeGetScriptVar},
		{"callScriptFn", nativeCallScriptFn},
		{"resetScript", nativeResetScript},
		{"freeScript", nativeFreeScript},
		{"checkScriptDirty", nativeCheckScriptDirty},
		{NULL, NULL}
	};

	//store the library in an aliased dictionary
	if (!TOY_IS_NULL(alias)) {
		//make sure the name isn't taken
		if (Toy_isDelcaredScopeVariable(interpreter->scope, alias)) {
			interpreter->errorOutput("Can't override an existing variable\n");
			Toy_freeLiteral(alias);
			return -1;
		}

		//create the dictionary to load up with functions
		Toy_LiteralDictionary* dictionary = TOY_ALLOCATE(Toy_LiteralDictionary, 1);
		Toy_initLiteralDictionary(dictionary);

		//load the dict with functions
		for (int i = 0; natives[i].name; i++) {
			Toy_Literal name = TOY_TO_STRING_LITERAL(Toy_createRefString(natives[i].name));
			Toy_Literal func = TOY_TO_FUNCTION_NATIVE_LITERAL(natives[i].fn);

			Toy_setLiteralDictionary(dictionary, name, func);

			Toy_freeLiteral(name);
			Toy_freeLiteral(func);
		}

		//build the type
		Toy_Literal type = TOY_TO_TYPE_LITERAL(TOY_LITERAL_DICTIONARY, true);
		Toy_Literal strType = TOY_TO_TYPE_LITERAL(TOY_LITERAL_STRING, true);
		Toy_Literal fnType = TOY_TO_TYPE_LITERAL(TOY_LITERAL_FUNCTION_NATIVE, true);
		TOY_TYPE_PUSH_SUBTYPE(&type, strType);
		TOY_TYPE_PUSH_SUBTYPE(&type, fnType);

		//set scope
		Toy_Literal dict = TOY_TO_DICTIONARY_LITERAL(dictionary);
		Toy_declareScopeVariable(interpreter->scope, alias, type);
		Toy_setScopeVariable(interpreter->scope, alias, dict, false);

		//cleanup
		Toy_freeLiteral(dict);
		Toy_freeLiteral(type);
		return 0;
	}

	//default
	for (int i = 0; natives[i].name; i++) {
		Toy_injectNativeFn(interpreter, natives[i].name, natives[i].fn);
	}

	return 0;
}

