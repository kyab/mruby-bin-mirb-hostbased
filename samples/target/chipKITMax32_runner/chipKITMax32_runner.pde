
#include <Servo.h>

#include "mruby.h"
#include "mruby/irep.h"
#include "mruby/string.h"
#include "mruby/value.h"
#include "mruby/dump.h"
#include "mruby/proc.h"

#ifdef MPIDE
//change heap size to 110kb
#define CHANGE_HEAP_SIZE(size) __asm__ volatile ("\t.globl _min_heap_size\n\t.equ _min_heap_size, " #size "\n")
CHANGE_HEAP_SIZE(115*1024);
#endif

#ifdef MPIDE
/*
 * Redirect stdout/stderr printf()/fwrite output to Serial with chipKIT
 * see http://www.chipkit.org/forum/viewtopic.php?f=7&t=2068
 * see 2.3 in http://hades.mech.northwestern.edu/images/f/fc/MPLAB_C32_Users_Guide.pdf
 */
extern "C"
{
  void _mon_putc(char s)
  {
    Serial.write(s);
    Serial.flush();
  }
}
#endif

//for Arduino Dur It looks like stdout is already redirected to the programming port UART on the due
//http://forum.arduino.cc/index.php/topic,148289.0.html


byte g_byteCodeBuf[2048];
mrb_state *mrb;
size_t total_allocated_mem = 0;

//required to link with mruby-arduino
void __dummy(){
  random(1,1);
  tone(1,2,3);
  pulseIn(1,2,3);
  shiftOut(1,2,3,4);
}

void reportMem(){
	char str[10];
	itoa(total_allocated_mem, str, 10);
	Serial.print("(taget):TOTAL_ALLOCATED : ");
	Serial.println(str);
}

// use custom allocator for detect out of memory
void *myallocf(mrb_state *mrb, void *p, size_t size, void *ud){
  if (size == 0){
    free(p);
    return NULL;
  }else{

    void *ret = realloc(p, size);

    if(!ret){
      char str[8];
      itoa(size, str, 8);
      Serial.print("(target):allocation error. requested size = ");
      Serial.println(str);
      reportMem();
    }else{
      total_allocated_mem += size;
    }
    return ret;
  }
}

//TODO: refactor this to inline byte waitRead()
inline void waitForReadAvailable(){
	while(Serial.available() <= 0);
}

void readByteCode(byte *buffer, int *len){
	byte soh = Serial.read();
	if (soh != 0x01){
		//something wrong!!! 
		Serial.println("(target):NON SOH received as start of data");
		return;
	}
	waitForReadAvailable();
	unsigned short lengthH = Serial.read();
	waitForReadAvailable();
	unsigned short lengthL = Serial.read();
	unsigned short lenToRead = (lengthH << 8 | lengthL);

	Serial.write('!');

	unsigned short lenReaded = 0;
	while(lenReaded < lenToRead){
		for (int i = 0 ; i < 100 ; i++){
			waitForReadAvailable();
			buffer[lenReaded] = Serial.read();
			lenReaded++;
			if (lenReaded == lenToRead){
				break;
			}
		}
		Serial.write('#');
	}

	*len = lenReaded;
}

void writeResult(const char *resultStr, int isException){

	unsigned short lenToWrite = (unsigned short)strlen(resultStr) + 1;
	byte lengthH = (byte)(lenToWrite >> 8);
	byte lengthL = (byte)(lenToWrite & 0xFF);

	const byte SOH = isException? 0x02 : 0x01;

	Serial.write(SOH);
	Serial.write(lengthH);
	Serial.write(lengthL);
	waitForReadAvailable();
	Serial.read();			//must be a '!'

	unsigned short lenWritten = 0;
	while(lenWritten < lenToWrite){
		for (int i = 0 ; i < 100 ;i++){
			Serial.write((byte)resultStr[lenWritten]);
			lenWritten++;
			if (lenWritten == lenToWrite){
				break;
			}
		}
		waitForReadAvailable();
		Serial.read();		//must be a '#'
	}
}

int ai;

void setup(){
  Serial.begin(9600);
  
  mrb = mrb_open_allocf(myallocf, NULL);
  ai = mrb_gc_arena_save(mrb);

 
  reportMem();

  //send HELLO message 
  Serial.print((byte)0x01); //SOH 
  Serial.print("HELLO");
  Serial.write((byte)'\0');
}


void loop(){

  if (Serial.available() > 0 ){

  	//receive bytecode
    int byteCodeLen = 0;
    readByteCode( g_byteCodeBuf, &byteCodeLen);

    //load bytecode
    FILE *fp = fmemopen(g_byteCodeBuf, byteCodeLen, "rb");
    int n = mrb_read_irep_file(mrb, fp);
  	if (n < 0) {
  		const char *resultStr = "(target):illegal bytecode.";
  		writeResult(resultStr, 1);
  		return;
  	}
  	fclose(fp);

  	//evaluate the bytecode
  	mrb_value result;
  	result = mrb_run(mrb, mrb_proc_new(mrb, mrb->irep[n]), mrb_top_self(mrb));

  	//prepare inspected string from result
    int exeption_on_run = mrb->exc? 1: 0;
  	if (exeption_on_run){
  		result = mrb_funcall(mrb, mrb_obj_value(mrb->exc),"inspect",0);
      mrb->exc = 0;
  	}else{
  		if (!mrb_respond_to(mrb, result, mrb_intern(mrb, "inspect"))){
  			result = mrb_any_to_s(mrb, result);
  		}else{
  			result = mrb_funcall(mrb, result, "inspect",0);
  		}
  	}

    //write result string back to host
    if (mrb->exc){  //failed to inspect
      mrb->exc = 0;
      const char *resultStr = "(target):too low memory to return anything.";
      writeResult(resultStr, 1);
    }else{
      writeResult(RSTRING_PTR(result), exeption_on_run);
    }

  	result = mrb_nil_value();	//meaningless??
  	// mrb_garbage_collect(mrb);
    mrb_gc_arena_restore(mrb, ai);

  }
  delay(10);
}

