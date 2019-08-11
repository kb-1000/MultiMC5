#include "jni.h"
#include "stdlib.h"
#include "string.h"

typedef jint (*Type_JNI_CreateJavaVM)(JavaVM **pvm, void **penv, void *args);

typedef struct jvm_functions
{
    Type_JNI_CreateJavaVM JNI_CreateJavaVM;
} jvm_functions;

static jvm_functions loadJVM(char *libPath)
{
    (void)libPath;
    return (jvm_functions){JNI_CreateJavaVM};
}

static char **decodeData(char *data, size_t *returnLength, size_t **returnArrayLength)
{
    char *decodedData[1024], currentDecode[1024], **returnMalloc, hex[3];
    size_t decodedLength, length, currentPosition, currentLength, i, computedReturnArrayLengths[1024];
    length = strlen(data);
    if (length == 0)
    {
        *returnLength = 0;
        return malloc(1);
    }
    memset(currentDecode, 0, 1024);
    decodedLength = 0;
    currentPosition = 0;
    for (i = 0; i < length; i++)
    {
        if (decodedLength >= 1024)
        {
            printf("Fatal error: argument can't be longer than 1024\n");
            exit(1);
        }
        if (data[i] == '_')
        {
            switch (data[++i])
            {
            case '_':
                currentDecode[currentPosition++] = '_';
                break;
            case 'n':
                currentLength = strlen(currentDecode);
                computedReturnArrayLengths[decodedLength] = currentLength;
                decodedData[decodedLength++] = strdup(currentDecode);
                memset(currentDecode, 0, 1024);
                currentPosition = 0;
                break;
            case 's':
                currentDecode[currentPosition++] = ' ';
                break;
            case 'x':
                hex[0] = data[++i];
                hex[1] = data[++i];
                hex[2] = 0;
                currentDecode[currentPosition++] = (char)strtoul(hex, NULL, 16);
                break;
                // TODO: default
            }
        }
        else
        {
            currentDecode[currentPosition++] = data[i];
        }
    }
    returnMalloc = malloc((decodedLength + 2) * sizeof(char *)); // segfaults with 1, why?
    if (returnArrayLength)
    {
        (*returnArrayLength) = malloc(decodedLength * sizeof(size_t));
    }
    // TODO: memcpy?
    for (i = 0; i < decodedLength; i++)
    {
        returnMalloc[i] = decodedData[i];
        if (returnArrayLength)
        {
            (*returnArrayLength)[i] = computedReturnArrayLengths[i];
        }
    }
    returnMalloc[decodedLength + 1] = NULL;
    *returnLength = decodedLength;
    return returnMalloc;
}

void recursive_free(char **ptr, size_t length)
{
    size_t i;
    for (i = 0; i < length; i++)
    {
        free(ptr[i]);
    }
    free(ptr);
}

static inline void checkException(JNIEnv *env)
{
    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionDescribe(env);
        exit(1);
    }
}

int main(int argc, char **argv)
{
    // TODO: move to a separate function to stop polluting the stack with all
    // these variables
    // TODO: decode main class name somehow too
    // TODO: actually print the stacktraces of all the errors, and not just general error messages
    jvm_functions functions;
    size_t jvmArgCount = 0, programArgCount = 0, i, *programArgLengths;
    JavaVM *jvm;
    JNIEnv *env;
    JavaVMInitArgs vm_args;
    JavaVMOption *options;
    jint error = JNI_OK;
    jclass stringClass, mainClass;
    char **jvmArgs, **programArgs;
    jmethodID stringConstructor, mainMethod;
    jobjectArray javaProgramArgs;
    jbyteArray byteArray;
    jstring javaProgramArg, utf8;
    if (argc != 5)
    {
        printf("Fatal error: expected 4 arguments, got %d\n", argc - 1);
        return 1;
    }
    functions = loadJVM(argv[2]);
    jvmArgs = decodeData(argv[1], &jvmArgCount, NULL);
    programArgs = decodeData(argv[4], &programArgCount, &programArgLengths);
    vm_args.version = JNI_VERSION_1_6;
    vm_args.nOptions = jvmArgCount;
    options = calloc(jvmArgCount, sizeof(JavaVMOption));
    for (i = 0; i < jvmArgCount; i++)
    {
        options[i].optionString = jvmArgs[i];
        // options[i].extraInfo is already NULL, due to calloc
    }
    vm_args.options = options;
    vm_args.ignoreUnrecognized = JNI_FALSE;
    error = functions.JNI_CreateJavaVM(&jvm, (void **)&env, &vm_args);
    recursive_free(jvmArgs, jvmArgCount);
    jvmArgs = NULL;
    free(options);
    options = vm_args.options = NULL;
    if (error != JNI_OK)
    {
        printf("Fatal error: failed to initialize JVM\n");
        recursive_free(programArgs, programArgCount);
        return error;
    }
    stringClass = (*env)->FindClass(env, "java/lang/String");
    checkException(env);
    if (!stringClass)
    {
        printf("Fatal error: could not find java.lang.String class\n");
        error = (*jvm)->DestroyJavaVM(jvm);
        if (error != JNI_OK)
        {
            printf("Error: could not destroy Java VM, exiting without cleanup\n");
            return error;
        }
        return 1;
    }
    stringConstructor = (*env)->GetMethodID(env, stringClass, "<init>", "([BLjava/lang/String;)V");
    checkException(env);
    javaProgramArgs = (*env)->NewObjectArray(env, programArgCount, stringClass, NULL);
    checkException(env);
    utf8 = (*env)->NewStringUTF(env, "UTF-8");
    for (i = 0; i < programArgCount; i++)
    {
        byteArray = (*env)->NewByteArray(env, programArgLengths[i]);
        checkException(env);
        (*env)->SetByteArrayRegion(env, byteArray, 0, programArgLengths[i], programArgs[i]);
        checkException(env);
        javaProgramArg = (*env)->NewObject(env, stringClass, stringConstructor, byteArray, utf8);
        checkException(env);
        (*env)->SetObjectArrayElement(env, javaProgramArgs, i, javaProgramArg); // XXX
        checkException(env);
    }

    recursive_free(programArgs, programArgCount);
    free(programArgLengths);

    mainClass = (*env)->FindClass(env, argv[3]);
    checkException(env);
    if (!mainClass)
    {
        printf("Fatal error: could not find main class\n");
        error = (*jvm)->DestroyJavaVM(jvm);
        if (error != JNI_OK)
        {
            printf("Error: could not destroy Java VM, exiting without cleanup\n");
            return error;
        }
        return 1;
    }
    mainMethod = (*env)->GetStaticMethodID(env, mainClass, "main", "([Ljava/lang/String;)V");
    checkException(env);
    if (!mainMethod)
    {
        printf("Fatal error: could not find main method on main class\n");
        error = (*jvm)->DestroyJavaVM(jvm);
        if (error != JNI_OK)
        {
            printf("Error: could not destroy Java VM, exiting without cleanup\n");
            return error;
        }
        return 1;
    }
    (*env)->CallStaticVoidMethod(env, mainClass, mainMethod, javaProgramArgs);
    checkException(env);
    error = (*jvm)->DestroyJavaVM(jvm);
    if (error != JNI_OK)
    {
        printf("Error: could not destroy Java VM, exiting without cleanup\n");
        return error;
    }
    return 0;
}
