/*
  Eccles Library holding custom typenames and common functions,this file holds every 
  Eccles abstraction library for the eccles tts executable
*/

#ifndef ECCLES_TYPES
#define ECCLES_TYPES

#include<stdint.h>
#include<iostream>
#include<fstream>
#include<string>

//Eccles core definitions
#define ECCLES_SYSTEM using namespace Eccles;
#define ECCLES_API namespace Eccles
#define ECCLES_API_ENTRY
#define eccles_main main





//Eccles namespace used in all Eccles projects
ECCLES_API {

//custom typenames

using e_uint = unsigned int;
using e_int = int;
using e_int16 = short;
using e_uint8 = uint8_t;
using e_uint16 = uint16_t;
using e_uint32 = uint32_t;
using e_float = float;
using e_double = double;
using e_boolean = bool;
using e_uint64 = uint64_t;
using e_string = const char*;
using e_char = char;
using e_strings = std::string;
using e_file = std::ifstream;

//custom values

#define e_true true
#define e_false false

//state definitions
 enum class State {
    OFF,ON
  };

  constexpr State ON = State::ON;
  constexpr State OFF = State::OFF;

  struct GLOBAL_STATE {
    e_boolean platformIO = false;
    e_string folderName = nullptr;
    e_string fileName = nullptr;
    e_string configPath = nullptr;
  };

  extern GLOBAL_STATE globalState; //handle to global data

};

#endif