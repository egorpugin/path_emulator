/*
local_settings:
    output_dir: .
    use_shared_libs: false
    build:
        cxx_flags_release: /MT
c++: 17
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

const String cl = "cl.exe";
const path dst = "links";

int main()
try
{
    if (primitives::resolve_executable(cl).empty())
        throw std::runtime_error("Please, run vcvars(32|64|all).bat file from VS installation");

    const auto obj = fs::temp_directory_path() / "path" / "obj";
    fs::create_directories(obj);
    fs::create_directories(dst);
    std::map<path, path> files;

    //
    auto root = YAML::LoadFile("path.yml");

    std::map<path, std::map<String, std::regex>> regex;
    get_map_and_iterate(root, "regex", [&regex](auto v)
    {
        auto re = get_sequence_set<String>(v.second);
        for (auto &r : re)
            regex[v.first.as<String>()].emplace(r, r);
    });

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

    // 1. dirs - only .exe files
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

    // 2. regex
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

    Executor e("Proxy creator");
    for (auto &f : files)
    {
        e.push([&obj, p = f.first, name = f.second]
        {
            auto o = dst / name;
            if (fs::exists(o))
                return;

            if (p.extension() == ".bat")
            {
                write_file(o, "@echo off\npushd .\n\"" + p.string() + "\" %*\npopd\n");
                return;
            }

            primitives::Command::execute({
                "cl.exe",
                "exe.cpp",
                "/Fo" + (obj / name).string(),
                "/Fe" + o.string(),
                "/nologo",
                "/EHsc",
                "/O2",
                "/TP",
                "/DUNICODE",
                "/DPROG=LR\"myfile(" + boost::replace_all_copy(p.parent_path().string(), "/", "\\") + "\\" +
                p.filename().string() + ")myfile\"",
                "/DPROG_NAME=LR\"myfile(" + p.filename().string() + ")myfile\"",
            });
        });
    }
    e.wait();

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
