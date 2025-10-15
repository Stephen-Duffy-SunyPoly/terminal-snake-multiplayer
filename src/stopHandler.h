/* cross platform header library for detecting the press of ctrl-c 
author: jSdCool
*/
#ifndef CBSTOPHANDLER_H
#define CBSTOPHANDLER_H

#ifdef _WIN32
	//windows inports
	#include <windows.h> //windows things
#else
	//unix / posix imports
	#include  <signal.h> //signal and SIGINT
	#include <stdlib.h> //exit

#endif

#include<stdbool.h> // booleans

#ifdef __cplusplus
	//namespcae for c++
	namespace stopHandler {
#endif

void (* USR_CTC_BREAK_HANDLER_FUNC)(void);

#ifdef _WIN32
	//windows stop catcher
	BOOL WINAPI CBSTOP_CtrlHandler(DWORD fdwCtrlType) {
		switch (fdwCtrlType) {
			// Handle the CTRL-C signal.
		case CTRL_C_EVENT:
			//call the user's function
			USR_CTC_BREAK_HANDLER_FUNC();
			//return false to tell windows to stop the program
			return FALSE;
		//optinaly handel other termination types here
		default:
			return FALSE;
		}
	}
#else
	//posix stop catcher
	void CBSTOP_CtrlHandler(int s){
		//call the user's function
		USR_CTC_BREAK_HANDLER_FUNC();
		//stop the program
		exit(0);
	}
#endif

/* this function sets the fucniton to be called in the event of ctrl-c being pressed
	args: void funct(void) : the function that sould be called when ctrl-c is pressed
	return: success of the action
*/
bool setContrlCHandler(void (* funct)(void) ){
	USR_CTC_BREAK_HANDLER_FUNC = funct;
	#ifdef _WIN32
		//windows handler
		return SetConsoleCtrlHandler(CBSTOP_CtrlHandler, TRUE);
	#else
		//posix handler
		signal(SIGINT, CBSTOP_CtrlHandler);//return prevous value so we can assume it wont fail
		return 1;
	#endif
}

#ifdef __cplusplus
	//close namespace
	}
#endif
//CBSTOPHANDLER_H
#endif
