#pragma once

// gate this here
#define TEMPORAL
#define DESIRE
#define TRAFFIC_CONVENTION

#include <array>
#include <memory>

#include "cereal/messaging/messaging.h"
#include "cereal/visionipc/visionipc_client.h"
#include "selfdrive/common/mat.h"
#include "selfdrive/common/modeldata.h"
#include "selfdrive/common/util.h"
#include "selfdrive/modeld/models/commonmodel.h"
#include "selfdrive/modeld/runners/run.h"

constexpr int DESIRE_LEN = 8;
constexpr int DESIRE_PRED_LEN = 4;
constexpr int TRAFFIC_CONVENTION_LEN = 2;
constexpr int MODEL_FREQ = 20;

constexpr int DISENGAGE_LEN = 5;
constexpr int BLINKER_LEN = 6;
constexpr int META_STRIDE = 7;

constexpr int PLAN_MHP_N = 5;
constexpr int STOP_LINE_MHP_N = 3;

constexpr int LANELINES_MHP_N = 4;

constexpr int LEAD_MHP_N = 2;
constexpr int LEAD_TRAJ_LEN = 6;
constexpr int LEAD_PRED_DIM = 4;
constexpr int LEAD_MHP_SELECTION = 3;

struct ModelOutputXYZ {
  float x;
  float y;
  float z;
};
static_assert(sizeof(ModelOutputXYZ) == sizeof(float)*3);

struct ModelOutputYZ {
  float y;
  float z;
};
static_assert(sizeof(ModelOutputYZ) == sizeof(float)*2);

struct ModelOutputPlanElement {
  ModelOutputXYZ position;
  ModelOutputXYZ velocity;
  ModelOutputXYZ acceleration;
  ModelOutputXYZ rotation;
  ModelOutputXYZ rotation_rate;
};
static_assert(sizeof(ModelOutputPlanElement) == sizeof(ModelOutputXYZ)*5);

struct ModelOutputPlanPrediction {
  std::array<ModelOutputPlanElement, TRAJECTORY_SIZE> mean;
  std::array<ModelOutputPlanElement, TRAJECTORY_SIZE> std;
  float prob;
};
static_assert(sizeof(ModelOutputPlanPrediction) == (sizeof(ModelOutputPlanElement)*TRAJECTORY_SIZE*2) + sizeof(float));

struct ModelOutputPlans {
  std::array<ModelOutputPlanPrediction, PLAN_MHP_N> prediction;

  constexpr const ModelOutputPlanPrediction &get_best_prediction() const {
    int max_idx = 0;
    for (int i = 1; i < prediction.size(); i++) {
      if (prediction[i].prob > prediction[max_idx].prob) {
        max_idx = i;
      }
    }
    return prediction[max_idx];
  }
};
static_assert(sizeof(ModelOutputPlans) == sizeof(ModelOutputPlanPrediction)*PLAN_MHP_N);

struct ModelOutputLineProbVal {
  float val_deprecated;
  float val;
};
static_assert(sizeof(ModelOutputLineProbVal) == sizeof(float)*2);

struct ModelOutputLinesProb {
  ModelOutputLineProbVal left_far;
  ModelOutputLineProbVal left_near;
  ModelOutputLineProbVal right_near;
  ModelOutputLineProbVal right_far;
};
static_assert(sizeof(ModelOutputLinesProb) == sizeof(ModelOutputLineProbVal)*4);

struct ModelOutputLaneLinesElement {
  std::array<ModelOutputYZ, TRAJECTORY_SIZE> mean;
  std::array<ModelOutputYZ, TRAJECTORY_SIZE>  std;
  std::array<float, LANELINES_MHP_N> prob;
};
static_assert(sizeof(ModelOutputLaneLinesElement) == (sizeof(ModelOutputYZ)*TRAJECTORY_SIZE*2) + sizeof(float)*LANELINES_MHP_N);

struct ModelOutputLaneLines {
  std::array<ModelOutputLaneLinesElement, LANELINES_MHP_N> prediction;
  ModelOutputLinesProb prob;

  constexpr const ModelOutputLaneLinesElement &get_lane_idx(int lane_idx) const {
    int max_idx = 0;
    for (int i = 1; i < prediction.size(); i++) {
      if (prediction[i].prob[lane_idx] > prediction[max_idx].prob[lane_idx]) {
        max_idx = i;
      }
    }
    return prediction[max_idx];
  }
};
static_assert(sizeof(ModelOutputLaneLines) == (sizeof(ModelOutputLaneLinesElement)*LANELINES_MHP_N) + (sizeof(ModelOutputLinesProb)));

struct ModelOutputEdgessXY {
  std::array<ModelOutputYZ, TRAJECTORY_SIZE> left;
  std::array<ModelOutputYZ, TRAJECTORY_SIZE> right;
};
static_assert(sizeof(ModelOutputEdgessXY) == sizeof(ModelOutputYZ)*TRAJECTORY_SIZE*2);

struct ModelOutputRoadEdges {
  ModelOutputEdgessXY mean;
  ModelOutputEdgessXY std;
};
static_assert(sizeof(ModelOutputRoadEdges) == (sizeof(ModelOutputEdgessXY)*2));

struct ModelOutputLeadElement {
  float x;
  float y;
  float velocity;
  float acceleration;
};
static_assert(sizeof(ModelOutputLeadElement) == sizeof(float)*4);

struct ModelOutputLeadPrediction {
  std::array<ModelOutputLeadElement, LEAD_TRAJ_LEN> mean;
  std::array<ModelOutputLeadElement, LEAD_TRAJ_LEN> std;
  std::array<float, LEAD_MHP_SELECTION> prob;
};
static_assert(sizeof(ModelOutputLeadPrediction) == (sizeof(ModelOutputLeadElement)*LEAD_TRAJ_LEN*2) + (sizeof(float)*LEAD_MHP_SELECTION));

struct ModelOutputLeads {
  std::array<ModelOutputLeadPrediction, LEAD_MHP_N> prediction;
  std::array<float, LEAD_MHP_SELECTION> prob;

  constexpr const ModelOutputLeadPrediction &get_best_prediction(int t_idx) const {
    int max_idx = 0;
    for (int i = 1; i < prediction.size(); i++) {
      if (prediction[i].prob[t_idx] > prediction[max_idx].prob[t_idx]) {
        max_idx = i;
      }
    }
    return prediction[max_idx];
  }
};
static_assert(sizeof(ModelOutputLeads) == (sizeof(ModelOutputLeadPrediction)*LEAD_MHP_N) + (sizeof(float)*LEAD_MHP_SELECTION));

struct ModelOutputStopLineElement {
  ModelOutputXYZ position;
  ModelOutputXYZ rotation;
  float speed;
  float time;
};
static_assert(sizeof(ModelOutputStopLineElement) == (sizeof(ModelOutputXYZ)*2 + sizeof(float)*2));

struct ModelOutputStopLinePrediction {
  ModelOutputStopLineElement mean;
  ModelOutputStopLineElement std;
  float prob;
};
static_assert(sizeof(ModelOutputStopLinePrediction) == (sizeof(ModelOutputStopLineElement)*2 + sizeof(float)));

struct ModelOutputStopLines {
  std::array<ModelOutputStopLinePrediction, STOP_LINE_MHP_N> prediction;
  float prob;

  constexpr const ModelOutputStopLinePrediction &get_best_prediction() const {
    int max_idx = 0;
    for (int i = 1; i < prediction.size(); i++) {
      if (prediction[i].prob > prediction[max_idx].prob) {
        max_idx = i;
      }
    }
    return prediction[max_idx];
  }
};
static_assert(sizeof(ModelOutputStopLines) == (sizeof(ModelOutputStopLinePrediction)*STOP_LINE_MHP_N) + sizeof(float));

struct ModelOutputPose {
  ModelOutputXYZ velocity_mean;
  ModelOutputXYZ rotation_mean;
  ModelOutputXYZ velocity_std;
  ModelOutputXYZ rotation_std;
};
static_assert(sizeof(ModelOutputPose) == sizeof(ModelOutputXYZ)*4);

struct ModelOutputDisengageProb {
  float gas_disengage;
  float brake_disengage;
  float steer_override;
  float brake_3ms2;
  float brake_4ms2;
  float brake_5ms2;
  float gas_pressed;
};
static_assert(sizeof(ModelOutputDisengageProb) == sizeof(float)*7);

struct ModelOutputBlinkerProb {
  float left;
  float right;
};
static_assert(sizeof(ModelOutputBlinkerProb) == sizeof(float)*2);

struct ModelOutputDesireProb {
  union {
    struct {
      float none;
      float turn_left;
      float turn_right;
      float lane_change_left;
      float lane_change_right;
      float keep_left;
      float keep_right;
      float null;
    };
    struct {
      std::array<float, DESIRE_LEN> array;
    };
  };
};
static_assert(sizeof(ModelOutputDesireProb) == sizeof(float)*DESIRE_LEN);

struct ModelOutputMeta {
  ModelOutputDesireProb desire_state_prob;
  float engaged_prob;
  std::array<ModelOutputDisengageProb, DISENGAGE_LEN> disengage_prob;
  std::array<ModelOutputBlinkerProb, BLINKER_LEN> blinker_prob;
  std::array<ModelOutputDesireProb, DESIRE_PRED_LEN> desire_pred_prob;
};
static_assert(sizeof(ModelOutputMeta) == sizeof(ModelOutputDesireProb) + sizeof(float) + (sizeof(ModelOutputDisengageProb)*DISENGAGE_LEN) + (sizeof(ModelOutputBlinkerProb)*BLINKER_LEN) + (sizeof(ModelOutputDesireProb)*DESIRE_PRED_LEN));

struct ModelOutput {
  const ModelOutputPlans plans;
  const ModelOutputLaneLines lane_lines;
  const ModelOutputRoadEdges road_edges;
  const ModelOutputLeads leads;
  const ModelOutputStopLines stop_lines;
  const ModelOutputMeta meta;
  const ModelOutputPose pose;
};

constexpr int OUTPUT_SIZE = sizeof(ModelOutput) / sizeof(float);
#ifdef TEMPORAL
  constexpr int TEMPORAL_SIZE = 512;
#else
  constexpr int TEMPORAL_SIZE = 0;
#endif
constexpr int NET_OUTPUT_SIZE = OUTPUT_SIZE + TEMPORAL_SIZE;

// TODO: convert remaining arrays to std::array and update model runners
struct ModelState {
  ModelFrame *frame;
  ModelFrame *wide_frame;
  std::array<float, NET_OUTPUT_SIZE> output = {};
  std::unique_ptr<RunModel> m;
#ifdef DESIRE
  float prev_desire[DESIRE_LEN] = {};
  float pulse_desire[DESIRE_LEN] = {};
#endif
#ifdef TRAFFIC_CONVENTION
  float traffic_convention[TRAFFIC_CONVENTION_LEN] = {};
#endif
};

void model_init(ModelState* s, cl_device_id device_id, cl_context context);
ModelOutput *model_eval_frame(ModelState* s, VisionBuf* buf, VisionBuf* buf_wide,
                              const mat3 &transform, const mat3 &transform_wide, float *desire_in);
void model_free(ModelState* s);
void model_publish(PubMaster &pm, uint32_t vipc_frame_id, uint32_t vipc_frame_id_extra, uint32_t frame_id, float frame_drop,
                   const ModelOutput &net_outputs, uint64_t timestamp_eof,
                   float model_execution_time, kj::ArrayPtr<const float> raw_pred, const bool valid);
void posenet_publish(PubMaster &pm, uint32_t vipc_frame_id, uint32_t vipc_dropped_frames,
                     const ModelOutput &net_outputs, uint64_t timestamp_eof, const bool valid);
