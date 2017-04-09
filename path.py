#!/usr/bin/python3
# -*- coding: utf-8 -*-

# "c:\Program Files (x86)\Microsoft Visual Studio 12.0\VC\bin\vcvars32.bat"

import argparse
import fnmatch
import glob
import multiprocessing
import os
import shutil
import subprocess

path_dir = 'g:\\dev\\path\\'
dst = path_dir + '\\links\\'
obj = 'obj\\'
prog = ''

def main():
    parser = argparse.ArgumentParser(description='generate path programs')
    parser.add_argument('--clean', dest='clean', action='store_true', help='clean previous files before build')
    pargs = parser.parse_args()

    prog = open('exe/exe.cpp').read()

    if shutil.which('cl.exe') == None:
        print('Please, run vcvars(32|64|all).bat file from VS')
        os.system('pause')
        return

    if pargs.clean == True:
        files = glob.glob(dst + '*')
        for f in files:
            os.remove(f)
        files = glob.glob(path_dir + obj + '*')
        for f in files:
            os.remove(f)

    exts = ['*.exe']
    paths = open(path_dir + 'path.txt', 'r').read().splitlines()
    cmds = []
    for dir in paths:
        if dir == '' or dir[0] == '#':
            continue
        args = dir.split('#')
        dir = args[0].strip()
        dir = os.path.abspath(dir)
        if not os.path.exists(dir):
            continue
        masks = exts
        if len(args) > 1:
            masks = args[1].split()
            for mask in masks:
                if mask.find(':') != -1:
                    old = mask
                    masks.remove(old)
                    mask = mask.split(':')
                    masks.insert(0, mask)
        for file in os.listdir(dir):
            for mask in masks:
                if (not isinstance(mask, list) and fnmatch.fnmatch(file, mask)) or (isinstance(mask, list) and fnmatch.fnmatch(file, mask[0])):
                    cpp = prog.replace('__fn__', file)
                    cpp = cpp.replace('__dn__', dir.replace('\\', '\\\\'))
                    if isinstance(mask, list):
                        fn = path_dir + obj + mask[1] + '.cpp'
                        fno = dst + mask[1]
                    else:
                        fn = path_dir + obj + file + '.cpp'
                        fno = dst + file
                    ext = os.path.splitext(file)[1]
                    if os.path.exists(fno):
                        continue
                    if ext != '.exe':
                        if ext == '.bat':
                            open(dst + file, 'w').write('@echo off\n\"' + dir + '\\' + file + '\" %*')
                        continue
                    f = open(fn, 'w')
                    f.write(cpp)
                    f.close()
                    cmds.append([
                                'cl',
                                fn,
                                '/Fo' + path_dir + obj + '\\',
                                '/Fe' + fno,
                                '/nologo',
                                '/EHsc',
                                '/O2',
                                '/TP'
                                ])
    multiprocessing.Pool().map(work, cmds)

def work(cmd):
    p = subprocess.Popen(cmd, stdout=subprocess.PIPE)
    out, _ = p.communicate()
    out = str(out, 'utf8')
    if len(out.splitlines()) > 1:
        print(out)

if __name__ == '__main__':
    main()
