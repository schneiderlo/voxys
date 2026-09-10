#pragma once

// Explicit native capture tooling. It drives the normal camera/detail controls, observes
// the real application, and saves its submitted surface before presentation.
#include "app/application.hpp"
#include "camera/camera.hpp"
#include "core/config.hpp"
#include "core/log.hpp"
#include "core/sha256.hpp"
#include "gpu/context.hpp"
#include <json.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace voxy {
class InspectionMotion {
    using Json = nlohmann::json;
    using Clock = std::chrono::steady_clock;
    enum class Phase { Waiting, Near, Running, End, Done, Failed };
public:
    static std::unique_ptr<InspectionMotion> create(const config::Config& config, std::string& error) {
        const auto& automation=config.automation;
        if (!automation.inspectionMotionRecipe && !automation.inspectionMotionOutput) return {};
        try {
            require(automation.inspectionMotionRecipe && !automation.inspectionMotionRecipe->empty()
                && automation.inspectionMotionOutput && !automation.inspectionMotionOutput->empty(),
                "Both nonempty inspection-motion recipe/output arguments are required");
            require(config.game.mode=="salvage" && config.game.assetFixtureRegistry && !config.game.assetFixtureRegistry->empty()
                && !automation.benchmark && !automation.screenshotPath && automation.screenshotTourCount==0,
                "Motion requires the asset inspector without another capture/benchmark mode");
            auto result=std::unique_ptr<InspectionMotion>(new InspectionMotion);
            const auto bytes=readBounded(*automation.inspectionMotionRecipe);
            result->recipe_=Json::parse(bytes);
            auto& recipe=result->recipe_;
            require(recipe.at("schema")==1 && recipe.at("trace_capacity")==8192,
                    "Unsupported inspection recipe schema/trace capacity");
            require(recipe.at("path")=="near to far to near; logarithmic distance, smoothstep on each half",
                    "Unsupported camera path");
            require(std::filesystem::absolute(recipe.at("registry").get<std::string>()).lexically_normal()
                ==std::filesystem::absolute(*config.game.assetFixtureRegistry).lexically_normal(),
                "Recipe must identify the actual selected registry");
            require(recipe.at("fov_degrees").get<double>()==static_cast<double>(config.camera.fov),
                    "Recipe FOV differs from the native configuration");
            result->duration_=number(recipe,"duration_seconds",10,60);
            result->near_=number(recipe,"near_distance_metres",1,100);
            result->far_=number(recipe,"far_distance_metres",result->near_+1,500);
            result->interval_=number(recipe,"minimum_capture_interval_ms",100,1000)/1000;
            result->capacity_=static_cast<size_t>(number(recipe,"frame_capacity",30,256));
            require(recipe.at("frame_capacity").is_number_unsigned(),"Capture capacity must be an integer");
            result->target_=vector(recipe.at("target"));
            result->ray_=vector(recipe.at("eye_direction"));
            const double length=glm::length(result->ray_);
            require(length>1e-6,"Camera eye direction must be nonzero");
            result->ray_/=length;
            require(std::abs(result->ray_.y)<.999,"Camera path cannot be vertical");
            result->yaw_=static_cast<float>(std::atan2(-result->ray_.x,-result->ray_.z));
            result->pitch_=static_cast<float>(std::asin(-result->ray_.y));
            const auto registryBytes=readBounded(*config.game.assetFixtureRegistry);
            const auto registry=Json::parse(registryBytes);
            result->registryEye_=vector(registry.at("camera").at("eye"));
            require(registry.at("schema")==1 || registry.at("schema")==2,"Motion requires an inspection registry");
            result->connected_=registry.at("schema")==2;
            require(result->connected_ || recipe.contains("inspection_expectations"),
                "Gallery motion requires an explicit workload");
            const auto forced=recipe.value("forced_lod",Json(0));
            require(forced.is_number_integer() && forced.get<int64_t>()>=0 && forced.get<int64_t>()<=3,
                "Forced detail must be an integer from 0 to 3");
            result->forcedLod_=forced.get<int>();
            result->expectations_=recipe.value("inspection_expectations",Json{
                {"parts",6},{"connections",5},{"uploads",3},{"prototype_uploads",1},
                {"model_draws",6},{"tracked_lod_placements",Json::array({0,1,2,3})}});
            const auto& expected=result->expectations_;
            for (const auto* key:{"parts","connections","uploads","prototype_uploads","model_draws"}) {
                require(expected.at(key).is_number_integer(),"Motion workload counts must be integers");
                static_cast<void>(number(expected,key,0,512));
            }
            require(expected.at("parts")==registry.at("placements").size()
                && expected.at("connections")== (result->connected_ ? registry.at("connections").size() : 0)
                && expected.at("parts").get<size_t>()>=1 && expected.at("parts").get<size_t>()<=32
                && expected.at("model_draws").get<size_t>()>=expected.at("parts").get<size_t>(),
                "Motion workload differs from the selected assembly");
            const auto& tracked=expected.at("tracked_lod_placements");
            require(tracked.is_array() && !tracked.empty() && tracked.size()<=32,
                "Motion tracked placements must be a bounded nonempty array");
            for (const auto& index:tracked) {
                require(index.is_number_integer() && index.get<int64_t>()>=0,"Motion tracked placement must be a nonnegative index");
                const auto i=index.get<size_t>();
                require(i<registry.at("placements").size() && !registry.at("placements").at(i).contains("prototype"),
                    "Motion tracked placement must identify an authored part");
                require(std::find(result->tracked_.begin(),result->tracked_.end(),i)==result->tracked_.end(),
                    "Motion tracked placements must be unique");
                result->tracked_.push_back(i);
            }
            for (const auto& placement:registry.at("placements")) result->prototypes_.push_back(placement.contains("prototype"));
            result->sequences_.resize(result->tracked_.size());
            result->output_=*automation.inspectionMotionOutput;
            require(std::filesystem::create_directory(result->output_),"Capture output must be a new directory");
            result->report_={{"status","running"},{"recipe",recipe},{"recipe_sha256",hash(bytes)},
                {"registry_sha256",hash(registryBytes)},{"scope","Actual native continuous camera and submitted-surface capture; readback/encoding overhead is not game performance"},
                {"frames",Json::array()}};
            LOG_INFO("Native inspection motion enabled: {} seconds; capture overhead is included",result->duration_);
            return result;
        } catch (const std::exception& exception) { error=exception.what(); return {}; }
    }

    void update(Application& app) {
        try {
            require(seconds(created_)<duration_+90,"Inspection motion exceeded its wall-clock bound");
            if (phase_==Phase::Waiting) {
                auto state=readState(app);
                if (!state.at("ready").get<bool>() || seconds(created_)<2) return;
                require(app.salvagePreviewAction(10+forcedLod_),"Requested inspection detail is unavailable");
                state=readState(app);
                validate(state);
                const auto& camera=state.at("camera");
                origin_=vector(camera.at("local"))+vector(camera.at("sector"))*256.0-registryEye_;
                target_+=origin_;
                report_["origin"]={origin_.x,origin_.y,origin_.z};report_["initial"]=state;
                auto* gpu=app.getGPUContext();
                require(gpu && recipe_.at("physical_viewport")==Json::array({gpu->getSwapchainWidth(),gpu->getSwapchainHeight()}),
                        "Actual physical viewport differs from the shared recipe");
                report_["render"]={{"render_width",gpu->getSwapchainWidth()},{"render_height",gpu->getSwapchainHeight()}};
                serial_=std::stoull(state.at("assetFixture").at("submittedSerial").get<std::string>());
                phase_=Phase::Near;
            }
            if (phase_==Phase::Near || phase_==Phase::Running || phase_==Phase::End) {
                elapsed_=phase_==Phase::Running ? seconds(started_) : 0;
                const double u=std::min(1.0,elapsed_/duration_);
                const double leg=u<=.5 ? 2*u : 2*(1-u);
                distance_=near_*std::pow(far_/near_,leg*leg*(3-2*leg));
                const auto eye=target_+ray_*distance_;
                auto* camera=app.getCamera();require(camera,"Camera disappeared during inspection");
                // Match the shipping browser camera API's world-sector and
                // final float narrowing exactly. No alternative projection.
                camera->setWorldPosition(glm::ivec3(0),glm::vec3(eye));
                camera->setYaw(yaw_);camera->setPitch(pitch_);
            }
        } catch (const std::exception& exception) { fail(app,exception.what()); }
    }

    void capture(Application& app) {
        try {
            if (phase_==Phase::Waiting || phase_==Phase::Failed || phase_==Phase::Done) return;
            auto state=readState(app);validate(state);
            auto* gpu=app.getGPUContext();
            require(gpu && recipe_.at("physical_viewport")==Json::array({gpu->getSwapchainWidth(),gpu->getSwapchainHeight()}),
                    "Capture viewport changed during motion");
            const auto completed=std::stoull(state.at("assetFixture").at("completedSerial").get<std::string>());
            if (phase_==Phase::Near) {
                if (completed>serial_+2) {report_["start"]=state;started_=Clock::now();phase_=Phase::Running;}
                return;
            }
            if (phase_==Phase::End) {
                if (completed>serial_+2) {phase_=Phase::Done;report_["final"]=state;app.requestExit();}
                return;
            }
            const auto eye=target_+ray_*distance_;
            require(state.at("assetFixture").at("draws")==expectations_.at("model_draws"),"Motion lost an expected model draw");
            Json sample={{"elapsed",elapsed_},{"distance",distance_},{"requested_eye",{eye.x,eye.y,eye.z}},{"state",state}};
            traceBytes_+=sample.dump().size();
            require(trace_.size()<8192 && traceBytes_<=16*1024*1024,"Inspection trace capacity exhausted");
            trace_.push_back(std::move(sample));
            for (size_t i=0;i<tracked_.size();++i) {
                const auto lod=state.at("assetFixture").at("lods").at(tracked_[i]).get<std::string>();
                if (sequences_[i].empty() || sequences_[i].back()!=lod) sequences_[i].push_back(lod);
            }
            const bool last=elapsed_>=duration_;
            if (last || report_["frames"].empty() || elapsed_-lastCapture_>=interval_) {
                require(report_["frames"].size()<capacity_,"Inspection image capacity exhausted");
                std::ostringstream filename;filename<<"frame-"<<std::setfill('0')<<std::setw(4)<<report_["frames"].size()<<".jpg";
                const double before=seconds(started_);
                require(app.captureScreenshot((output_/filename.str()).string(),Application::CaptureFormat::Jpeg),
                        "Native frame readback or image write failed");
                report_["frames"].push_back({{"filename",filename.str()},{"before_seconds",before},
                    {"after_seconds",seconds(started_)},{"pose_seconds",elapsed_},{"state",state}});
                lastCapture_=elapsed_;
            }
            if (last) {
                const auto expected=forcedLod_ ? std::vector<std::string>{std::to_string(forcedLod_)}
                                              : std::vector<std::string>{"1","2","3","2","1"};
                for (const auto& sequence:sequences_) require(sequence==expected,
                    "Actual LOD sequence missed a level, oscillated, or failed to return");
                serial_=std::stoull(state.at("assetFixture").at("submittedSerial").get<std::string>());
                phase_=Phase::End;
            }
        } catch (const std::exception& exception) { fail(app,exception.what()); }
    }

    bool finish() {
        try {
            const auto trace=trace_.dump()+"\n";
            write(output_/"trace.json",trace);
            report_["trace"]={{"samples",trace_.size()},{"sha256",hash(trace)},{"seconds",trace_.empty()?0:trace_.back().at("elapsed").get<double>()}};
            report_["lod_sequences"]=sequences_;
            report_["tracked_lod_placements"]=tracked_;
            if (phase_!=Phase::Done && !report_.contains("error")) report_["error"]="Application closed before capture and real GPU completion finished";
            report_["status"]=phase_==Phase::Done ? "captured; visual review required" : "failed";
            write(output_/"native-report.json",report_.dump(2)+"\n");
            return phase_==Phase::Done;
        } catch (const std::exception& exception) { LOG_ERROR("Failed to save inspection evidence: {}",exception.what()); return false; }
    }
private:
    static void require(bool condition,const char* message) { if (!condition) throw std::runtime_error(message); }
    static double seconds(Clock::time_point start) {return std::chrono::duration<double>(Clock::now()-start).count();}
    static std::string readBounded(const std::filesystem::path& path) {
        std::ifstream file(path,std::ios::binary);require(file.good(),"Cannot open motion input");
        std::array<char,65537> bytes{};file.read(bytes.data(),static_cast<std::streamsize>(bytes.size()));
        const auto size=file.gcount();require(size>0 && size<=65536,"Motion input must be 1..65536 bytes");
        return {bytes.data(),static_cast<size_t>(size)};
    }
    static std::string hash(std::string_view text) {return core::sha256Hex(core::sha256(std::as_bytes(std::span{text.data(),text.size()})));}
    static void write(const std::filesystem::path& path,const std::string& bytes) {
        std::ofstream file(path,std::ios::binary);file.exceptions(std::ios::badbit|std::ios::failbit);file<<bytes;
    }
    static double number(const Json& object,const char* name,double minimum,double maximum) {
        const double value=object.at(name).get<double>();
        require(std::isfinite(value) && value>=minimum && value<=maximum,"Motion parameter is outside its finite bounds");return value;
    }
    static glm::dvec3 vector(const Json& values) {
        require(values.is_array() && values.size()==3,"Motion vector requires three components");
        glm::dvec3 result;
        for (int i=0;i<3;++i) {result[i]=values.at(static_cast<size_t>(i)).get<double>();require(std::isfinite(result[i]) && std::abs(result[i])<=1000,"Motion coordinate is outside its finite bounds");}
        return result;
    }
    static Json readState(const Application& app) {
        const auto bytes=app.salvagePreviewJson();require(bytes.size()<=65536,"Inspection state exceeded its byte bound");return Json::parse(bytes);
    }
    void validate(const Json& state) const {
        require(state.at("ready")==true && state.at("failed")==false && state.at("bodies")==0,"Assembly failed or gained scenery bodies");
        const auto& asset=state.at("assetFixture");
        require(connected_ ? asset.at("assembly")==Json({{"parts",expectations_.at("parts")},{"connections",expectations_.at("connections")}})
                           : !asset.contains("assembly"),"Actual assembly differs from the declared registry kind");
        require(asset.at("uploads")==expectations_.at("uploads")
            && asset.at("prototypeUploads")==expectations_.at("prototype_uploads")
            && asset.at("forcedLod")==std::to_string(forcedLod_) && asset.at("guides")==0,
            "Actual assembly/detail/residency differs from the declared workload");
        require(asset.at("lods").size()==prototypes_.size(),"Motion placement detail count changed");
        for (size_t i=0;i<prototypes_.size();++i)
            require(asset.at("lods").at(i).is_null()==prototypes_[i],"Authored or prototype LOD kind changed");
        require(state.at("session").at("builds")==0,"Capture changed canonical construction authority");
        if (report_.contains("initial")) {
            const auto& initial=report_.at("initial");
            require(asset.at("generation")==initial.at("assetFixture").at("generation")
                && asset.at("gpuReservationBytes")==initial.at("assetFixture").at("gpuReservationBytes")
                && state.at("session").at("inventory")==initial.at("session").at("inventory"),"Capture changed resident resources or inventory");
        }
    }
    void fail(Application& app,const std::string& message) {phase_=Phase::Failed;report_["error"]=message;LOG_ERROR("Native inspection motion failed: {}",message);app.requestExit();}
    Phase phase_=Phase::Waiting;
    Clock::time_point created_=Clock::now(),started_{};
    Json recipe_,report_,expectations_,trace_=Json::array();
    std::filesystem::path output_;
    glm::dvec3 target_{},ray_{},registryEye_{},origin_{};
    double duration_=30,near_=6,far_=100,interval_=.15,elapsed_=0,distance_=6,lastCapture_=0;
    float yaw_=0,pitch_=0;
    size_t capacity_=256,traceBytes_=0;
    uint64_t serial_=0;
    int forcedLod_=0;
    bool connected_=true;
    std::vector<size_t> tracked_;
    std::vector<bool> prototypes_;
    std::vector<std::vector<std::string>> sequences_;
};
} // namespace voxy
