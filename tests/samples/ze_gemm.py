import os
import subprocess
import sys

import utils

def config(path):
  if (sys.platform != 'win32'):
    cmake = ["cmake", "-DCMAKE_BUILD_TYPE=" + utils.get_build_flag(), ".."]
  else:
    cmake = ["cmake", "-G", "NMake Makefiles", "-DCMAKE_BUILD_TYPE=" + utils.get_build_flag(), ".."]
  stdout, stderr = utils.run_process(cmake, path)
  if stderr and stderr.find("CMake Error") != -1:
    return stderr
  return None

def build(path):
  if (sys.platform != 'win32'):
    stdout, stderr = utils.run_process(["make"], path)
  else:
    stdout, stderr = utils.run_process(["nmake"], path)
  if stderr and stderr.lower().find("error") != -1:
    return stderr
  return None

def run(path):
  if (sys.platform != 'win32'):
    command = ["./ze_gemm", "1024", "1"]
  else:
    command = ["ze_gemm", "1024", "1"]

  stdout, stderr = utils.run_process(command, path)
  if stderr:
    return stderr
  if not stdout:
    return "stdout is empty"
  if stdout.find(" CORRECT") == -1:
    return stdout
  return None

def main(option):
  path = utils.get_sample_build_path("ze_gemm")
  log = config(path)
  if log:
    return log
  log = build(path)
  if log:
    return log
  log = run(path)
  if log:
    return log

if __name__ == "__main__":
  log = main(None)
  if log:
    print(log)
