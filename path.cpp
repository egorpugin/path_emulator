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

/*
local_settings:
    output_dir: .
    use_shared_libs: false
    build:
        cxx_flags_release: /MT
c++: 17
options:
    any:
        definitions:
            win32:
                public:
                    - UNICODE
dependencies:
    - pvt.egorpugin.primitives.command: master
    - pvt.egorpugin.primitives.executor: master
    - pvt.egorpugin.primitives.filesystem: master
    - pvt.egorpugin.primitives.yaml: master
*/

#include <primitives/command.h>
#include <primitives/executor.h>
#include <primitives/filesystem.h>
#include <primitives/yaml.h>

#include <boost/algorithm/string.hpp>

#include <iostream>
#include <regex>
#include <set>

#include <Windows.h>

const String cl = "cl.exe";
const path dst = "links";
const path exe = "exe.cpp";

short DumpFile(LPCTSTR filename)
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

int main()
try
{
    if (primitives::resolve_executable(cl).empty())
        throw std::runtime_error("Please, run vcvars(32|64|all).bat file from VS installation");
    if (!fs::exists(exe))
        throw std::runtime_error("exe.cpp was not found");

    const auto obj = fs::temp_directory_path() / "path" / "obj";
    fs::create_directories(obj);
    fs::create_directories(dst);
    std::map<path, path> files;

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
            if (!fs::is_regular_file(file) || (file.path().extension() != ".exe" && file.path().extension() != ".bat"))
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
        for (auto &e : v.second)
        {
            if (e.IsScalar())
                files.emplace(p / e.as<String>(), e.as<String>());
            else if (e.IsMap())
                files.emplace(p / e.begin()->first.as<String>(), e.begin()->second.as<String>());
        }
    });

    Executor e("Proxy creator");
    std::vector<Future<void>> futures;
    futures.reserve(files.size());
    for (auto &f : files)
    {
        futures.push_back(e.push([&obj, p = f.first, name = f.second]
        {
            auto o = dst / name;
            if (fs::exists(o))
                return;

            if (p.extension() == ".bat")
            {
                write_file(o, "@echo off\npushd .\ncall \"" + p.string() + "\" %*\npopd\n");
                return;
            }

            auto ss = DumpFile(wnormalize_path(p).c_str());

            Strings args{
                cl,
                exe.string(),
                "/Fo" + (obj / name).string(),
                "/Fe" + o.string(),
                "/nologo",
                "/EHsc",
                "/O2",
                "/TP",
                "/DUNICODE",
                "/DCONSOLE="s + (ss == IMAGE_SUBSYSTEM_WINDOWS_GUI ? "0" : "1"),
                "/DPROG=LR\"myfile(" + boost::replace_all_copy(p.parent_path().string(), "/", "\\") + "\\" + p.filename().string() + ")myfile\"",
                "/DPROG_NAME=LR\"myfile(" + p.filename().string() + ")myfile\"",
            };

            args.push_back("-link");
            args.push_back("Shell32.lib");
            if (ss == IMAGE_SUBSYSTEM_WINDOWS_GUI)
                args.push_back("/SUBSYSTEM:WINDOWS");

            static std::mutex m;
            {
                //std::unique_lock<std::mutex> lk(m);
                //std::cout << "building " << o << " from " << p << "\n";
            }

            primitives::Command c;
            c.args = args;
            std::error_code ec;
            c.execute(ec);
            if (ec)
            {
                std::unique_lock<std::mutex> lk(m);
                std::cout << "building " << o << " from " << p << "\n";
                std::cout << c.out.text << "\n";
                std::cout << c.err.text << "\n";
            }
        }));
    }
    waitAndGet(futures);

    return 0;
}
catch (const std::exception &e)
{
    std::cerr << e.what() << "\n";
    return 1;
}
catch (...)
{
    std::cerr << "Unhandled unknown exception" << "\n";
    return 1;
}
