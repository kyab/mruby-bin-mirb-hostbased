
#include <Servo.h>

//#define MRB_WORD_BOXING
//#define MRB_USE_FLOAT
//#define MRB_NAN_BOXING

#ifdef ARDUINO
#if ARDUINO>=152  //for Due only
#include "itoa.h"
#endif
#endif

#include "mruby.h"
#include "mruby/irep.h"
#include "mruby/string.h"
#include "mruby/value.h"
#include "mruby/dump.h"
#include "mruby/proc.h"

extern "C" {
extern void codedump_all(mrb_state *mrb, int n);
}

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

#ifdef ARDUINO
#if ARDUINO>=152  //for Due only
//for Arduino Due It looks like stdout is already redirected to the programming port UART.
//http://forum.arduino.cc/index.php/topic,148289.0.html
#endif
#endif


//DEBUG PRINT
//http://gcc.gnu.org/onlinedocs/cpp/Variadic-Macros.html
#define DPRINTF(fmt,...) if(verbose) printf("(target:)debug:"fmt,##__VA_ARGS__)

byte g_byteCodeBuf[2048];
mrb_state *mrb;
size_t total_allocated_mem = 0;

//required to link with mruby-arduino
void __dummy(){
  random(1,1);
#ifdef MPIDE
  tone(1,2,3);
#endif
  pulseIn(1,2,3);
  shiftOut(1,2,3,4);
}

void reportMem(){
	char str[15];
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
      char str[15];
      itoa(size, str, 10);
      Serial.print("(target):allocation error. requested size = ");
      Serial.println(str);
      reportMem();
    }else{
      total_allocated_mem += size;
    }
    return ret;
  }
}

inline bool waitForReadAvailable(int waitMs = 1000){
  int start_ms = millis();
	while( (millis() - start_ms) < waitMs){
    if (Serial.available() > 0) {
      return true;
    }
  }
  return false;
}

bool readByteCode(byte *buffer, int *len, int *verbose){
	byte soh = Serial.read();
	if ((soh == 0x01 || soh == 0x02)) {
  
  }else if (soh == 0x05) { // ENQ
    //send ACK 
    Serial.write((byte)0x06);
    return false;
  } else {
    //something wrong!!! 
    Serial.println("(target):NON SOH received as start of data");
    return false;    
  }
  *verbose = (soh == 0x02) ? 1 : 0;

	if (!waitForReadAvailable()) return false;
	unsigned short lengthH = Serial.read();
	if (!waitForReadAvailable()) return false;
	unsigned short lengthL = Serial.read();
	unsigned short lenToRead = (lengthH << 8 | lengthL);

	Serial.write('!');

	unsigned short lenReaded = 0;
	while(lenReaded < lenToRead){
		for (int i = 0 ; i < 100 ; i++){
			if (!waitForReadAvailable()) return false;
			buffer[lenReaded] = Serial.read();
			lenReaded++;
			if (lenReaded == lenToRead){
				break;
			}
		}
		Serial.write('#');
	}

	*len = lenReaded;
  return true;
}

void writeResult(const char *resultStr, int isException){

	unsigned short lenToWrite = (unsigned short)strlen(resultStr) + 1;
	byte lengthH = (byte)(lenToWrite >> 8);
	byte lengthL = (byte)(lenToWrite & 0xFF);

	const byte SOH = isException? 0x02 : 0x01;

	Serial.write(SOH);
	Serial.write(lengthH);
	Serial.write(lengthL);
	if (!waitForReadAvailable()) return;
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
		if (!waitForReadAvailable()) return;
		Serial.read();		//must be a '#'
	}
}

int ai;

void setup(){
  Serial.begin(9600);
  
  mrb = mrb_open_allocf(myallocf, NULL);
  ai = mrb_gc_arena_save(mrb);

  reportMem();

#ifdef MPIDE
  Serial.write((byte)0x06);   //ACK witout ENQ
#endif

}

void readEvalPrint(){
  int verbose = 0;

  //receive bytecode
  int byteCodeLen = 0;
  if (!readByteCode( g_byteCodeBuf, &byteCodeLen, &verbose))
    return;


  DPRINTF("readByteCode done\n",0);

  //load bytecode
  FILE *fp = fmemopen(g_byteCodeBuf, byteCodeLen, "rb");
  int n = mrb_read_irep_file(mrb, fp);
  if (n < 0) {
    const char *resultStr = "(target):illegal bytecode.";
    writeResult(resultStr, 1);
    return;
  }
  fclose(fp);

  DPRINTF("mirb_read_ire_file done.\n");
  if (verbose) codedump_all(mrb, n);

  //evaluate the bytecode
  mrb_value result;
  result = mrb_run(mrb, mrb_proc_new(mrb, mrb->irep[n]), mrb_top_self(mrb));

  DPRINTF("mrb_run done. exception = %d\n", (int)mrb->exc);

  mrb_value result_str;
  //prepare inspected string from result
  int exeption_on_run = mrb->exc? 1: 0;
  if (exeption_on_run){
    result_str = mrb_funcall(mrb, mrb_obj_value(mrb->exc),"inspect",0);
    mrb->exc = 0;
  }else{
    DPRINTF("asking #inspect possibility to result\n");
    if (!mrb_respond_to(mrb, result, mrb_intern(mrb, "inspect"))){
      result_str = mrb_any_to_s(mrb, result);
    }else{
      result_str = mrb_funcall(mrb, result, "inspect",0);
    }
    mrb_gc_mark_value(mrb, result);
  }

  if (mrb->exc == 0) {
    DPRINTF("inspected result:%s\n", RSTRING_PTR(result_str));
  }

  //write result string back to host
  if (mrb->exc){  //failed to inspect
    mrb->exc = 0;
    const char *msg = "(target):too low memory to return anything.";
    writeResult(msg, 1);
  }else{
    writeResult(RSTRING_PTR(result_str), exeption_on_run);
    mrb_gc_mark_value(mrb, result_str);
  }

  mrb_garbage_collect(mrb);      //??
  mrb_gc_arena_restore(mrb, ai);  //??

}

void loop(){

  if (Serial.available() > 0 ){
    readEvalPrint();
  }
  delay(10);
}

