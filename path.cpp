/*
local_settings:
    output_dir: .
    use_shared_libs: false
    build:
        cxx_flags_release: /MT
c++: 17
files: path.cpp
options:
    any:
        definitions:
            win32:
                public:
                    - UNICODE
dependencies:
    - pub.egorpugin.primitives.command: master
    - pub.egorpugin.primitives.executor: master
    - pub.egorpugin.primitives.filesystem: master
    - pub.egorpugin.primitives.log: master
    - pub.egorpugin.primitives.yaml: master
    - pub.egorpugin.primitives.sw.main: master
*/

/*
 * Copyright (C) 2016-2018, Egor Pugin
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <primitives/command.h>
#include <primitives/executor.h>
#include <primitives/filesystem.h>
#include <primitives/yaml.h>
#include <primitives/sw/main.h>

#include <boost/algorithm/string.hpp>

#include <iostream>
#include <regex>

#include <Windows.h>

#include <primitives/log.h>
DECLARE_STATIC_LOGGER(logger, "path");

WORD get_file_subsystem(LPCTSTR filename)
{
    auto hFile = CreateFile(filename, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        //printf("Couldn't open file with CreateFile()\n");
        return 0;
    }

    auto hFileMapping = CreateFileMapping(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
    if (hFileMapping == 0)
    {
        CloseHandle(hFile);
        //printf("Couldn't open file mapping with CreateFileMapping()\n");
        return 0;
    }

    auto lpFileBase = MapViewOfFile(hFileMapping, FILE_MAP_READ, 0, 0, 0);
    if (lpFileBase == 0)
    {
        CloseHandle(hFileMapping);
        CloseHandle(hFile);
        //printf("Couldn't map view of file with MapViewOfFile()\n");
        return 0;
    }

    auto dosHeader = (PIMAGE_DOS_HEADER)lpFileBase;
    auto fh = (PIMAGE_NT_HEADERS)((char*)dosHeader + dosHeader->e_lfanew);
    auto ss = fh->OptionalHeader.Subsystem;

    UnmapViewOfFile(lpFileBase);
    CloseHandle(hFileMapping);
    CloseHandle(hFile);

    return ss;
}

void remove_old_files(const FilesMap &files, const path &dir)
{
    // get new files
    Files dstfiles;
    for (auto &[k, v] : files)
        dstfiles.insert(v);

    // get existing links
    auto existing_files = enumerate_files(dir);
    for (auto &f : existing_files)
    {
        if (dstfiles.find(f.filename()) == dstfiles.end())
        {
            std::cout << "Removing old file: " << f << "\n";
            fs::remove(f);
        }
    }
}

void create_links(const FilesMap &files, const path &dstdir, const path &compiler, const path &exe_source_fn)
{
    const auto obj = fs::temp_directory_path() / "path" / "obj";
    fs::create_directories(obj);

    Executor e("Proxy creator");
    std::vector<Future<void>> futures;
    for (auto &f : files)
    {
        futures.push_back(e.push([&obj, &dstdir, &compiler, &exe_source_fn, p = f.first, name = f.second]
        {
            auto o = dstdir / name;
            if (fs::exists(o))
                return;

            if (p.extension() == ".bat" || p.extension() == ".cmd")
            {
                write_file(o, "@echo off\npushd .\ncall \"" + p.string() + "\" %*\npopd\n");
                return;
            }

            auto ss = get_file_subsystem(wnormalize_path(p).c_str());

            Strings args
            {
                compiler.u8string(),
                exe_source_fn.u8string(),
                "/Fo" + normalize_path(obj / name),
                "/Fe" + normalize_path(fs::current_path() / o),
                "/nologo",
                "/EHsc",

    #ifndef NDEBUG
                "/Od",
                "/Zi",
                "/DDEBUG_EXE",
    #else
                "/O2",
    #endif

                "/DUNICODE",
                "/DCONSOLE="s + (ss == IMAGE_SUBSYSTEM_WINDOWS_GUI ? "0" : "1"),
                "/DPROG=LR\"myfile(" + boost::replace_all_copy(normalize_path(p.parent_path()), "/", "\\") + "\\" + normalize_path(p.filename()) + ")myfile\"",
                "/DPROG_NAME=LR\"myfile(" + normalize_path(p.filename()) + ")myfile\"",
            };

            args.push_back("-link");

    #ifndef NDEBUG
            args.push_back("-debug:full");
    #endif

            args.push_back("Shell32.lib");
            if (ss == IMAGE_SUBSYSTEM_WINDOWS_GUI)
                args.push_back("/SUBSYSTEM:WINDOWS");

            primitives::Command c;
    #ifndef NDEBUG
            c.inherit = true;
    #endif
            c.setArguments(args);
            c.execute();
        }));
    }
    waitAndGet(futures);
}

int main(int argc, char **argv)
{
    const String cl = "cl.exe";
    const path exe = "exe.cpp";

    if (primitives::resolve_executable(cl).empty())
        throw std::runtime_error("Please, run vcvars(32|64|all).bat file from VS installation");
    if (!fs::exists(exe))
        throw std::runtime_error("exe.cpp was not found");

    FilesMap files;

    //
    auto root = YAML::LoadFile("path.yml");

    // 1. all .exe and .bat files, example:
    /*
    all:
        - "c:/miktex/texmfs/install/miktex/bin/"
    */
    for (auto &p : get_sequence_set<path, String>(root, "all"))
    {
        if (!fs::exists(p))
        {
            std::cout << p << " does not exist\n";
            continue;
        }

        for (auto &file : boost::make_iterator_range(fs::directory_iterator(p), {}))
        {
            if (!fs::is_regular_file(file) ||
                (file.path().extension() != ".exe" &&
                file.path().extension() != ".bat" &&
                file.path().extension() != ".cmd"
                ))
                continue;
            files.emplace(file.path(), file.path().filename());
        }
    }

    // 2. regex, example:
    /*
    regex:
        "c:/Program Files (x86)/Microsoft Visual Studio/2017/Community/VC/Auxiliary/Build":
            - .*\.bat$
    */
    std::map<path, std::map<String, std::regex>> regex;
    get_map_and_iterate(root, "regex", [&regex](auto v)
    {
        auto re = get_sequence_set<String>(v.second);
        for (auto &r : re)
            regex[v.first.as<String>()].emplace(r, r);
    });
    for (auto &re : regex)
    {
        auto &p = re.first;
        if (!fs::exists(p))
        {
            std::cout << p << " does not exist\n";
            continue;
        }

        for (auto &file : boost::make_iterator_range(fs::directory_iterator(p), {}))
        {
            if (!fs::is_regular_file(file))
                continue;
            for (auto &r : re.second)
            {
                if (!std::regex_match(file.path().string(), r.second))
                    continue;
                files.emplace(file.path(), file.path().filename());
                break;
            }
        }
    }

    // 3. files, example:
    /*
    files:
        "c:/Program Files/PostgreSQL/10/bin/":
            - psql.exe
    */
    get_map_and_iterate(root, "files", [&files](auto v)
    {
        path p = v.first.as<String>();
        for (const auto &e : v.second)
        {
            if (e.IsScalar())
                files.emplace(p / e.as<String>(), e.as<String>());
            else if (e.IsMap())
                files.emplace(p / e.begin()->first.as<String>(), e.begin()->second.as<String>());
        }
    });

    const path dst = "links";
    fs::create_directories(dst);
    remove_old_files(files, dst);
    create_links(files, dst, cl, exe);

    return 0;
}
