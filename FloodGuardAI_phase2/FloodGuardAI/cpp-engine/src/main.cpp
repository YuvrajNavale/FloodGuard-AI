// floodguard_engine
//   --version
//   --scenario <file.json> [--frame-interval N] [--out <file>]   batch run, JSON on stdout (or file)
//   --serve                                                       JSON-lines session on stdin/stdout
// Phase 2 (additive; without these options the Phase 1 behaviour and output are unchanged):
//   --infra-out FILE     write infrastructure + road-graph validation JSON for the scenario's town
//   --infra-seed N       override the infrastructure seed       --no-bridges   do not generate bridges
//   --impact-out FILE    run the scenario, then write the infrastructure-impact report JSON
//   --basis final|peak   depth grid used for the impact report (default final)
// stdout carries ONLY JSON. Diagnostics go to stderr.
#include "impact_io.hpp"
#include "infra_io.hpp"
#include "io.hpp"
#include "road_graph.hpp"
#include "simulator.hpp"
#include "terrain.hpp"
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>

using namespace fg;

static const char* kVersion = "0.1.0";

static std::string err_json(const std::string& m) { return "{\"ok\":false,\"error\":" + jstr(m) + "}"; }

static std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open file: " + path);
    std::stringstream ss; ss << in.rdbuf(); return ss.str();
}

static std::string run_batch(const Config& cfg, int frame_interval) {
    Grid grid = build_town(cfg);
    Simulator sim(cfg, grid);
    std::string series = "[" + step_to_json(sim.snapshot()), frames;
    bool first_frame = true;
    auto add_frame = [&](const StepResult& r) {
        if (!first_frame) frames += ',';
        first_frame = false;
        frames += "{\"step\":" + jnum(r.step) + ",\"time_min\":" + jnum(r.time_min)
                + ",\"depth_m\":" + depth_to_json(sim.depths()) + "}";
    };
    add_frame(sim.snapshot());
    StepResult last = sim.snapshot();
    while (!sim.finished()) {
        last = sim.step();
        series += "," + step_to_json(last);
        if (frame_interval > 0 && last.step % frame_interval == 0) add_frame(last);
        if (last.status == "error_unstable") break;
    }
    series += "]";
    if (frame_interval > 0 && last.step % frame_interval != 0) add_frame(last);
    std::string out = "{\"ok\":true,\"engine_version\":\"" + std::string(kVersion) + "\",\"scenario\":" + config_to_json(cfg);
    out += ",\"town\":" + town_to_json(sim.grid());
    out += ",\"series\":" + series + ",\"frames\":[" + frames + "]";
    out += ",\"final\":" + step_to_json(last) + ",\"final_depth_m\":" + depth_to_json(sim.depths());
    out += ",\"max_depth_per_cell_m\":" + depth_to_json(sim.max_depth_per_cell()) + "}";
    return out;
}

static int serve() {
    std::unique_ptr<Simulator> sim;
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        try {
            Json req = Json::parse(line);
            std::string cmd = req.str("cmd", "");
            if (cmd == "quit") { std::cout << "{\"ok\":true,\"bye\":true}" << std::endl; return 0; }
            if (cmd == "version") { std::cout << "{\"ok\":true,\"engine_version\":\"" << kVersion << "\"}" << std::endl; continue; }
            if (cmd == "init") {
                const Json* sc = req.find("scenario");
                if (!sc) throw std::invalid_argument("init requires 'scenario'");
                Config cfg = config_from_json(*sc);
                Grid g = build_town(cfg);
                sim.reset(new Simulator(cfg, g));
                std::cout << "{\"ok\":true,\"town\":" << town_to_json(sim->grid())
                          << ",\"total_steps\":" << sim->total_steps()
                          << ",\"state\":" << step_to_json(sim->snapshot()) << "}" << std::endl;
                continue;
            }
            if (!sim) throw std::runtime_error("no scenario loaded: send 'init' first");
            if (cmd == "step" || cmd == "run_to_end") {
                int n = cmd == "step" ? std::max(1, req.integer("n", 1)) : 1 << 30;
                bool depth = req.boolean("include_depth", false);
                StepResult r = sim->snapshot();
                std::vector<int> newly;
                for (int k = 0; k < n && !sim->finished(); ++k) {
                    r = sim->step();
                    newly.insert(newly.end(), r.newly_inundated_ids.begin(), r.newly_inundated_ids.end());
                    if (r.status == "error_unstable") break;
                }
                r.newly_inundated_ids = newly;
                std::cout << "{\"ok\":true,\"state\":" << step_to_json(r, true);
                if (depth) std::cout << ",\"depth_m\":" << depth_to_json(sim->depths());
                std::cout << "}" << std::endl;
            } else if (cmd == "get_state") {
                std::cout << "{\"ok\":true,\"state\":" << step_to_json(sim->snapshot());
                if (req.boolean("include_depth", false)) std::cout << ",\"depth_m\":" << depth_to_json(sim->depths());
                std::cout << "}" << std::endl;
            } else if (cmd == "reset") {
                sim->reset();
                std::cout << "{\"ok\":true,\"state\":" << step_to_json(sim->snapshot()) << "}" << std::endl;
            } else if (cmd == "set_rainfall") {
                if (req.has("intensity_mm_h") && !sim->set_rain_intensity(req.num("intensity_mm_h", 0)))
                    throw std::invalid_argument("cannot set intensity (negative value or time_varying rainfall)");
                if (req.has("rain_scale")) sim->set_rain_scale(req.num("rain_scale", 1.0));
                std::cout << "{\"ok\":true,\"state\":" << step_to_json(sim->snapshot()) << "}" << std::endl;
            } else {
                throw std::invalid_argument("unknown cmd: " + cmd);
            }
        } catch (const std::exception& e) {
            std::cerr << "[engine] error: " << e.what() << std::endl;
            std::cout << err_json(e.what()) << std::endl;
        }
    }
    return 0;
}

int main(int argc, char** argv) {
    std::string scenario, out_path, infra_out, impact_out, basis = "final"; int frame_interval = 0; bool do_serve = false;
    bool seed_set = false, no_bridges = false; int infra_seed = 1;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--version") { std::cout << "{\"engine_version\":\"" << kVersion << "\"}" << std::endl; return 0; }
        else if (a == "--serve") do_serve = true;
        else if (a == "--scenario" && i + 1 < argc) scenario = argv[++i];
        else if (a == "--frame-interval" && i + 1 < argc) frame_interval = std::atoi(argv[++i]);
        else if (a == "--out" && i + 1 < argc) out_path = argv[++i];
        else if (a == "--infra-out" && i + 1 < argc) infra_out = argv[++i];
        else if (a == "--infra-seed" && i + 1 < argc) { infra_seed = std::atoi(argv[++i]); seed_set = true; }
        else if (a == "--no-bridges") no_bridges = true;
        else if (a == "--impact-out" && i + 1 < argc) impact_out = argv[++i];
        else if (a == "--basis" && i + 1 < argc) basis = argv[++i];
        else { std::cerr << "usage: floodguard_engine --version | --serve | --scenario FILE [--frame-interval N] [--out FILE] [--infra-out FILE] [--infra-seed N] [--no-bridges] [--impact-out FILE] [--basis final|peak]\n"; return 2; }
    }
    if (do_serve) return serve();
    if (scenario.empty()) { std::cerr << "no mode given (use --serve or --scenario)\n"; return 2; }
    try {
        Json root = Json::parse(read_file(scenario));
        Config cfg = config_from_json(root);
        InfraConfig icfg = infra_config_from_json(root);
        if (seed_set) icfg.seed = infra_seed;
        if (no_bridges) icfg.enable_bridges = false;
        if (basis != "final" && basis != "peak") throw std::invalid_argument("--basis must be final or peak");
        bool phase2_only = (!infra_out.empty() || !impact_out.empty()) && out_path.empty();   // Phase 2 output requested, no explicit Phase 1 file
        if (!infra_out.empty()) {
            Grid grid = build_town(cfg);
            Infrastructure infra = build_infrastructure(grid, icfg);
            GraphValidation gv = RoadGraph(infra).validate();
            std::ofstream f(infra_out, std::ios::binary);
            if (!f) throw std::runtime_error("cannot write file: " + infra_out);
            f << "{\"ok\":true,\"engine_version\":\"" << kVersion << "\",\"scenario_name\":" << jstr(cfg.name)
              << ",\"infrastructure\":" << infrastructure_to_json(infra, grid)
              << ",\"graph_validation\":" << validation_to_json(gv) << "}\n";
            std::cerr << "[engine] wrote " << infra_out << "\n";
        }
        if (!impact_out.empty()) {
            ImpactConfig icf = impact_config_from_json(root, cfg);
            icf.basis = basis == "peak" ? DepthBasis::Peak : DepthBasis::Final;
            Grid grid = build_town(cfg);
            Infrastructure infra = build_infrastructure(grid, icfg);
            GraphValidation gv = RoadGraph(infra).validate();
            Simulator sim(cfg, grid);
            StepResult last = sim.snapshot();
            while (!sim.finished()) { last = sim.step(); if (last.status == "error_unstable") break; }
            if (last.status == "error_unstable") throw std::runtime_error("simulation became unstable; no impact report written");
            ImpactReport rep = analyze_impact(grid, infra, basis == "peak" ? sim.max_depth_per_cell() : sim.depths(), icf);
            std::ofstream f(impact_out, std::ios::binary);
            if (!f) throw std::runtime_error("cannot write file: " + impact_out);
            f << impact_to_json(rep, grid, infra, gv, cfg.name, step_to_json(last)) << "\n";
            std::cerr << "[engine] wrote " << impact_out << " (basis=" << basis << ")\n";
        }
        if (phase2_only) {
            std::cout << "{\"ok\":true";
            if (!infra_out.empty()) std::cout << ",\"infra_out\":" << jstr(infra_out);
            if (!impact_out.empty()) std::cout << ",\"impact_out\":" << jstr(impact_out);
            std::cout << "}" << std::endl; return 0;
        }
        std::string out = run_batch(cfg, frame_interval);
        if (out_path.empty()) std::cout << out << std::endl;
        else { std::ofstream(out_path) << out << "\n"; std::cerr << "[engine] wrote " << out_path << "\n"; }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "[engine] error: " << e.what() << std::endl;
        std::cout << err_json(e.what()) << std::endl;
        return 3;
    }
}
