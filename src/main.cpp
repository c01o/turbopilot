#include <iostream>
#include <cstdio>
#include <ggml/ggml.h>

#include <spdlog/spdlog.h>

#include <turbopilot/crow_all.h>

#include <argparse/argparse.hpp>

#include "turbopilot/model.hpp"
#include "turbopilot/starcoder.hpp"
#include "turbopilot/gptj.hpp"
#include "turbopilot/gptneox.hpp"
#include "turbopilot/server.hpp"
#include "turbopilot/replit.hpp"

int main(int argc, char **argv)
{

    argparse::ArgumentParser program("turbopilot");

    program.add_argument("-f", "--model-file")
        .help("Path to the model that turbopilot should serve")
        .required();

    program.add_argument("-m", "--model-type")
        .help("The type of model to load. Can be codegen,starcoder,wizardcoder,stablecode,replit")
        .default_value("codegen");

    program.add_argument("-t", "--threads")
        .help("The number of CPU threads turbopilot is allowed to use. Defaults to 4")
        .default_value(4)
        .scan<'i', int>();


    program.add_argument("-p", "--port")
        .help("The tcp port that turbopilot should listen on")
        .default_value(18080)
        .scan<'i', int>();

    program.add_argument("-r", "--random-seed")
        .help("Set the random seed for RNG functions")
        .default_value(-1)
        .scan<'i', int>();

    program.add_argument("prompt").remaining();


    try
    {
        program.parse_args(argc, argv);
    }
    catch (const std::runtime_error &err)
    {
        std::cerr << err.what() << std::endl;
        std::cerr << program;
        return 1;
    }

    ggml_time_init();

    const int64_t t_main_start_us = ggml_time_us();


    TurbopilotModel *model = NULL;

    auto model_type = program.get<std::string>("--model-type");

    ModelConfig config{};
    std::mt19937 rng(program.get<int>("--random-seed"));

    config.n_threads = program.get<int>("--threads");

    if(model_type.compare("codegen") == 0) {
        spdlog::info("Initializing GPT-J type model for '{}' model", model_type);
        model = new GPTJModel(config, rng);
    }else if(model_type.compare("starcoder") == 0 || model_type.compare("wizardcoder") == 0){
        spdlog::info("Initializing Starcoder/Wizardcoder type model for '{}' model type", model_type);
        model = new StarcoderModel(config, rng);
    }else if(model_type.compare("stablecode") == 0){
        spdlog::info("Initializing StableLM type model for '{}' model type", model_type);
        model = new GPTNEOXModel(config, rng);
    }else if(model_type.compare("replit") == 0){
        spdlog::info("Initializing Replit type model for '{}' model type", model_type);
        model = new ReplitModel(config, rng);
    }else{
        spdlog::error("Invalid model type: {}", model_type);
    }

    spdlog::info("Attempt to load model for {}", program.get<std::string>("--model-type"));
    int64_t t_load_us = 0;
    const int64_t t_start_us = ggml_time_us();
    auto loaded = model->load_model(program.get<std::string>("--model-file"));

    if(!loaded){
        spdlog::error("Failed to load model");
        return -1;
    }

    t_load_us = ggml_time_us() - t_start_us;

    spdlog::info("Loaded model in {:0.2f}ms", t_load_us/1000.0f);


    crow::SimpleApp app;

    CROW_ROUTE(app, "/")([](){
        return "Hello world";
    });

    CROW_ROUTE(app, "/copilot_internal/v2/token")([](){
        //return "Hello world";

        crow::json::wvalue response = {{"token","1"}, {"expires_at", static_cast<std::uint64_t>(2600000000)}, {"refresh_in",900}};

        crow::response res;
        res.code = 200;
        res.set_header("Content-Type", "application/json");
        res.body = response.dump();
        return res;
    });


    CROW_ROUTE(app, "/v1/completions").methods(crow::HTTPMethod::Post)
    ([&model](const crow::request& req) {
        return serve_response(model, req);
    });

    CROW_ROUTE(app, "/v1/engines/codegen/completions").methods(crow::HTTPMethod::Post)
    ([&model](const crow::request& req) {
        return serve_response(model, req);
    });


    CROW_ROUTE(app, "/v1/engines/copilot-codex/completions").methods(crow::HTTPMethod::Post)
    ([&model](const crow::request& req) {
        return serve_response(model, req);
    });

    app.port(program.get<int>("--port")).multithreaded().run();

    free(model);
}