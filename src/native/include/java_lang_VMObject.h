/* This file is machine generated, don't edit it !*/

/* Structure information for class: java/lang/VMObject */

typedef struct java_lang_VMObject {
   java_objectheader header;
} java_lang_VMObject;

/*
 * Class:     java/lang/VMObject
 * Method:    clone
 * Signature: (Ljava/lang/Cloneable;)Ljava/lang/Object;
 */
JNIEXPORT struct java_lang_Object* JNICALL Java_java_lang_VMObject_clone (JNIEnv *env , jclass clazz , struct java_lang_Cloneable* par1);
/*
 * Class:     java/lang/VMObject
 * Method:    notify
 * Signature: (Ljava/lang/Object;)V
 */
JNIEXPORT void JNICALL Java_java_lang_VMObject_notify (JNIEnv *env , jclass clazz , struct java_lang_Object* par1);
/*
 * Class:     java/lang/VMObject
 * Method:    notifyAll
 * Signature: (Ljava/lang/Object;)V
 */
JNIEXPORT void JNICALL Java_java_lang_VMObject_notifyAll (JNIEnv *env , jclass clazz , struct java_lang_Object* par1);
/*
 * Class:     java/lang/VMObject
 * Method:    wait
 * Signature: (Ljava/lang/Object;JI)V
 */
JNIEXPORT void JNICALL Java_java_lang_VMObject_wait (JNIEnv *env , jclass clazz , struct java_lang_Object* par1, s8 par2, s4 par3);
