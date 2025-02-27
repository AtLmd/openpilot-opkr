#include "selfdrive/modeld/models/driving.h"

#include <fcntl.h>
#include <unistd.h>

#include <cassert>
#include <cstring>

#include <eigen3/Eigen/Dense>

#include "selfdrive/common/clutil.h"
#include "selfdrive/common/params.h"
#include "selfdrive/common/timing.h"

constexpr float FCW_THRESHOLD_5MS2_HIGH = 0.15;
constexpr float FCW_THRESHOLD_5MS2_LOW = 0.05;
constexpr float FCW_THRESHOLD_3MS2 = 0.7;

std::array<float, 5> prev_brake_5ms2_probs = {0,0,0,0,0};
std::array<float, 3> prev_brake_3ms2_probs = {0,0,0};

// #define DUMP_YUV

template<class T, size_t size>
constexpr const kj::ArrayPtr<const T> to_kj_array_ptr(const std::array<T, size> &arr) {
  return kj::ArrayPtr(arr.data(), arr.size());
}

void model_init(ModelState* s, cl_device_id device_id, cl_context context) {
  s->frame = new ModelFrame(device_id, context);
  s->wide_frame = new ModelFrame(device_id, context);

#ifdef USE_THNEED
  s->m = std::make_unique<ThneedModel>("models/supercombo.thneed",
#elif USE_ONNX_MODEL
  s->m = std::make_unique<ONNXModel>("models/supercombo.onnx",
#else
  s->m = std::make_unique<SNPEModel>("models/supercombo.dlc",
#endif
   &s->output[0], NET_OUTPUT_SIZE, USE_GPU_RUNTIME, true);

#ifdef TEMPORAL
  s->m->addRecurrent(&s->output[OUTPUT_SIZE], TEMPORAL_SIZE);
#endif

#ifdef DESIRE
  s->m->addDesire(s->pulse_desire, DESIRE_LEN);
#endif

#ifdef TRAFFIC_CONVENTION
  const int idx = Params().getBool("IsRHD") ? 1 : 0;
  s->traffic_convention[idx] = 1.0;
  s->m->addTrafficConvention(s->traffic_convention, TRAFFIC_CONVENTION_LEN);
#endif
}

ModelOutput* model_eval_frame(ModelState* s, VisionBuf* buf, VisionBuf* wbuf,
                              const mat3 &transform, const mat3 &transform_wide, float *desire_in) {
#ifdef DESIRE
  if (desire_in != NULL) {
    for (int i = 1; i < DESIRE_LEN; i++) {
      // Model decides when action is completed
      // so desire input is just a pulse triggered on rising edge
      if (desire_in[i] - s->prev_desire[i] > .99) {
        s->pulse_desire[i] = desire_in[i];
      } else {
        s->pulse_desire[i] = 0.0;
      }
      s->prev_desire[i] = desire_in[i];
    }
  }
#endif

  // if getInputBuf is not NULL, net_input_buf will be
  auto net_input_buf = s->frame->prepare(buf->buf_cl, buf->width, buf->height, transform, static_cast<cl_mem*>(s->m->getInputBuf()));
  s->m->addImage(net_input_buf, s->frame->buf_size);

  if (wbuf != nullptr) {
    auto net_extra_buf = s->wide_frame->prepare(wbuf->buf_cl, wbuf->width, wbuf->height, transform_wide, static_cast<cl_mem*>(s->m->getExtraBuf()));
    s->m->addExtra(net_extra_buf, s->wide_frame->buf_size);
  }
  s->m->execute();

  return (ModelOutput*)&s->output;
}

void model_free(ModelState* s) {
  delete s->frame;
}

void fill_lead(cereal::ModelDataV2::LeadDataV3::Builder lead, const ModelOutputLeads &leads, int t_idx, float prob_t) {
  std::array<float, LEAD_TRAJ_LEN> lead_t = {0.0, 2.0, 4.0, 6.0, 8.0, 10.0};
  const auto &best_prediction = leads.get_best_prediction(t_idx);
  lead.setProb(sigmoid(leads.prob[t_idx]));
  lead.setProbTime(prob_t);
  std::array<float, LEAD_TRAJ_LEN> lead_x, lead_y, lead_v, lead_a;
  std::array<float, LEAD_TRAJ_LEN> lead_x_std, lead_y_std, lead_v_std, lead_a_std;
  for (int i=0; i<LEAD_TRAJ_LEN; i++) {
    lead_x[i] = best_prediction.mean[i].x;
    lead_y[i] = best_prediction.mean[i].y;
    lead_v[i] = best_prediction.mean[i].velocity;
    lead_a[i] = best_prediction.mean[i].acceleration;
    lead_x_std[i] = exp(best_prediction.std[i].x);
    lead_y_std[i] = exp(best_prediction.std[i].y);
    lead_v_std[i] = exp(best_prediction.std[i].velocity);
    lead_a_std[i] = exp(best_prediction.std[i].acceleration);
  }
  lead.setT(to_kj_array_ptr(lead_t));
  lead.setX(to_kj_array_ptr(lead_x));
  lead.setY(to_kj_array_ptr(lead_y));
  lead.setV(to_kj_array_ptr(lead_v));
  lead.setA(to_kj_array_ptr(lead_a));
  lead.setXStd(to_kj_array_ptr(lead_x_std));
  lead.setYStd(to_kj_array_ptr(lead_y_std));
  lead.setVStd(to_kj_array_ptr(lead_v_std));
  lead.setAStd(to_kj_array_ptr(lead_a_std));
}

// added by opkr
void fill_stop_line(cereal::ModelDataV2::StopLineData::Builder stop_line, const ModelOutputStopLines &stop_lines) {
  const auto &best_data = stop_lines.get_best_prediction();
  stop_line.setProb(sigmoid(stop_lines.prob));

  stop_line.setX(best_data.mean.position.x);
  stop_line.setY(best_data.mean.position.y);
  stop_line.setZ(best_data.mean.position.z);
  stop_line.setRoll(best_data.mean.rotation.x);
  stop_line.setPitch(best_data.mean.rotation.y);
  stop_line.setYaw(best_data.mean.rotation.z);
  stop_line.setSpeedAtLine(best_data.mean.speed);
  stop_line.setSecondsUntilLine(best_data.mean.time);

  stop_line.setXStd(best_data.std.position.x);
  stop_line.setYStd(best_data.std.position.y);
  stop_line.setZStd(best_data.std.position.z);
  stop_line.setRollStd(best_data.std.rotation.x);
  stop_line.setPitchStd(best_data.std.rotation.y);
  stop_line.setYawStd(best_data.std.rotation.z);
  stop_line.setSpeedAtLineStd(best_data.std.speed);
  stop_line.setSecondsUntilLineStd(best_data.std.time);
}

void fill_meta(cereal::ModelDataV2::MetaData::Builder meta, const ModelOutputMeta &meta_data) {
  std::array<float, DESIRE_LEN> desire_state_softmax;
  softmax(meta_data.desire_state_prob.array.data(), desire_state_softmax.data(), DESIRE_LEN);

  std::array<float, DESIRE_PRED_LEN * DESIRE_LEN> desire_pred_softmax;
  for (int i=0; i<DESIRE_PRED_LEN; i++) {
    softmax(meta_data.desire_pred_prob[i].array.data(), desire_pred_softmax.data() + (i * DESIRE_LEN), DESIRE_LEN);
  }

  std::array<float, DISENGAGE_LEN> lat_long_t = {2,4,6,8,10};
  std::array<float, DISENGAGE_LEN> gas_disengage_sigmoid, brake_disengage_sigmoid, steer_override_sigmoid,
                                   brake_3ms2_sigmoid, brake_4ms2_sigmoid, brake_5ms2_sigmoid;
  for (int i=0; i<DISENGAGE_LEN; i++) {
    gas_disengage_sigmoid[i] = sigmoid(meta_data.disengage_prob[i].gas_disengage);
    brake_disengage_sigmoid[i] = sigmoid(meta_data.disengage_prob[i].brake_disengage);
    steer_override_sigmoid[i] = sigmoid(meta_data.disengage_prob[i].steer_override);
    brake_3ms2_sigmoid[i] = sigmoid(meta_data.disengage_prob[i].brake_3ms2);
    brake_4ms2_sigmoid[i] = sigmoid(meta_data.disengage_prob[i].brake_4ms2);
    brake_5ms2_sigmoid[i] = sigmoid(meta_data.disengage_prob[i].brake_5ms2);
    //gas_pressed_sigmoid[i] = sigmoid(meta_data.disengage_prob[i].gas_pressed);
  }

  std::memmove(prev_brake_5ms2_probs.data(), &prev_brake_5ms2_probs[1], 4*sizeof(float));
  std::memmove(prev_brake_3ms2_probs.data(), &prev_brake_3ms2_probs[1], 2*sizeof(float));
  prev_brake_5ms2_probs[4] = brake_5ms2_sigmoid[0];
  prev_brake_3ms2_probs[2] = brake_3ms2_sigmoid[0];

  bool above_fcw_threshold = true;
  for (int i=0; i<prev_brake_5ms2_probs.size(); i++) {
    float threshold = i < 2 ? FCW_THRESHOLD_5MS2_LOW : FCW_THRESHOLD_5MS2_HIGH;
    above_fcw_threshold = above_fcw_threshold && prev_brake_5ms2_probs[i] > threshold;
  }
  for (int i=0; i<prev_brake_3ms2_probs.size(); i++) {
    above_fcw_threshold = above_fcw_threshold && prev_brake_3ms2_probs[i] > FCW_THRESHOLD_3MS2;
  }

  auto disengage = meta.initDisengagePredictions();
  disengage.setT(to_kj_array_ptr(lat_long_t));
  disengage.setGasDisengageProbs(to_kj_array_ptr(gas_disengage_sigmoid));
  disengage.setBrakeDisengageProbs(to_kj_array_ptr(brake_disengage_sigmoid));
  disengage.setSteerOverrideProbs(to_kj_array_ptr(steer_override_sigmoid));
  disengage.setBrake3MetersPerSecondSquaredProbs(to_kj_array_ptr(brake_3ms2_sigmoid));
  disengage.setBrake4MetersPerSecondSquaredProbs(to_kj_array_ptr(brake_4ms2_sigmoid));
  disengage.setBrake5MetersPerSecondSquaredProbs(to_kj_array_ptr(brake_5ms2_sigmoid));

  meta.setEngagedProb(sigmoid(meta_data.engaged_prob));
  meta.setDesirePrediction(to_kj_array_ptr(desire_pred_softmax));
  meta.setDesireState(to_kj_array_ptr(desire_state_softmax));
  meta.setHardBrakePredicted(above_fcw_threshold);
}

template<size_t size>
void fill_xyzt(cereal::ModelDataV2::XYZTData::Builder xyzt, const std::array<float, size> &t,
               const std::array<float, size> &x, const std::array<float, size> &y, const std::array<float, size> &z) {
  xyzt.setT(to_kj_array_ptr(t));
  xyzt.setX(to_kj_array_ptr(x));
  xyzt.setY(to_kj_array_ptr(y));
  xyzt.setZ(to_kj_array_ptr(z));
}

template<size_t size>
void fill_xyzt(cereal::ModelDataV2::XYZTData::Builder xyzt, const std::array<float, size> &t,
               const std::array<float, size> &x, const std::array<float, size> &y, const std::array<float, size> &z,
               const std::array<float, size> &x_std, const std::array<float, size> &y_std, const std::array<float, size> &z_std) {
  fill_xyzt(xyzt, t, x, y, z);
  xyzt.setXStd(to_kj_array_ptr(x_std));
  xyzt.setYStd(to_kj_array_ptr(y_std));
  xyzt.setZStd(to_kj_array_ptr(z_std));
}

void fill_plan(cereal::ModelDataV2::Builder &framed, const ModelOutputPlanPrediction &plan) {
  std::array<float, TRAJECTORY_SIZE> pos_x, pos_y, pos_z;
  std::array<float, TRAJECTORY_SIZE> pos_x_std, pos_y_std, pos_z_std;
  std::array<float, TRAJECTORY_SIZE> vel_x, vel_y, vel_z;
  std::array<float, TRAJECTORY_SIZE> rot_x, rot_y, rot_z;
  std::array<float, TRAJECTORY_SIZE> rot_rate_x, rot_rate_y, rot_rate_z;

  for(int i=0; i<TRAJECTORY_SIZE; i++) {
    pos_x[i] = plan.mean[i].position.x;
    pos_y[i] = plan.mean[i].position.y;
    pos_z[i] = plan.mean[i].position.z;
    pos_x_std[i] = exp(plan.std[i].position.x);
    pos_y_std[i] = exp(plan.std[i].position.y);
    pos_z_std[i] = exp(plan.std[i].position.z);
    vel_x[i] = plan.mean[i].velocity.x;
    vel_y[i] = plan.mean[i].velocity.y;
    vel_z[i] = plan.mean[i].velocity.z;
    rot_x[i] = plan.mean[i].rotation.x;
    rot_y[i] = plan.mean[i].rotation.y;
    rot_z[i] = plan.mean[i].rotation.z;
    rot_rate_x[i] = plan.mean[i].rotation_rate.x;
    rot_rate_y[i] = plan.mean[i].rotation_rate.y;
    rot_rate_z[i] = plan.mean[i].rotation_rate.z;
  }

  fill_xyzt(framed.initPosition(), T_IDXS_FLOAT, pos_x, pos_y, pos_z, pos_x_std, pos_y_std, pos_z_std);
  fill_xyzt(framed.initVelocity(), T_IDXS_FLOAT, vel_x, vel_y, vel_z);
  fill_xyzt(framed.initOrientation(), T_IDXS_FLOAT, rot_x, rot_y, rot_z);
  fill_xyzt(framed.initOrientationRate(), T_IDXS_FLOAT, rot_rate_x, rot_rate_y, rot_rate_z);
}

void fill_lane_lines(cereal::ModelDataV2::Builder &framed, const std::array<float, TRAJECTORY_SIZE> &plan_t,
                     const ModelOutputLaneLines &lanes) {

  const auto &left_far = lanes.get_lane_idx(0);
  const auto &left_near = lanes.get_lane_idx(1);
  const auto &right_near = lanes.get_lane_idx(2);
  const auto &right_far = lanes.get_lane_idx(3);
  std::array<float, TRAJECTORY_SIZE> left_far_y, left_far_z;
  std::array<float, TRAJECTORY_SIZE> left_near_y, left_near_z;
  std::array<float, TRAJECTORY_SIZE> right_near_y, right_near_z;
  std::array<float, TRAJECTORY_SIZE> right_far_y, right_far_z;
  for (int j=0; j<TRAJECTORY_SIZE; j++) {
    left_far_y[j] = left_far.mean[j].y;
    left_far_z[j] = left_far.mean[j].z;
    left_near_y[j] = left_near.mean[j].y;
    left_near_z[j] = left_near.mean[j].z;
    right_near_y[j] = right_near.mean[j].y;
    right_near_z[j] = right_near.mean[j].z;
    right_far_y[j] = right_far.mean[j].y;
    right_far_z[j] = right_far.mean[j].z;
  }

  auto lane_lines = framed.initLaneLines(4);
  fill_xyzt(lane_lines[0], plan_t, X_IDXS_FLOAT, left_far_y, left_far_z);
  fill_xyzt(lane_lines[1], plan_t, X_IDXS_FLOAT, left_near_y, left_near_z);
  fill_xyzt(lane_lines[2], plan_t, X_IDXS_FLOAT, right_near_y, right_near_z);
  fill_xyzt(lane_lines[3], plan_t, X_IDXS_FLOAT, right_far_y, right_far_z);

  framed.setLaneLineStds({
    exp(left_far.std[0].y),
    exp(left_near.std[0].y),
    exp(right_near.std[0].y),
    exp(right_far.std[0].y),
  });

  framed.setLaneLineProbs({
    sigmoid(lanes.prob.left_far.val),
    sigmoid(lanes.prob.left_near.val),
    sigmoid(lanes.prob.right_near.val),
    sigmoid(lanes.prob.right_far.val),
  });
}

void fill_road_edges(cereal::ModelDataV2::Builder &framed, const std::array<float, TRAJECTORY_SIZE> &plan_t,
                     const ModelOutputRoadEdges &edges) {
  std::array<float, TRAJECTORY_SIZE> left_y, left_z;
  std::array<float, TRAJECTORY_SIZE> right_y, right_z;
  for (int j=0; j<TRAJECTORY_SIZE; j++) {
    left_y[j] = edges.mean.left[j].y;
    left_z[j] = edges.mean.left[j].z;
    right_y[j] = edges.mean.right[j].y;
    right_z[j] = edges.mean.right[j].z;
  }

  auto road_edges = framed.initRoadEdges(2);
  fill_xyzt(road_edges[0], plan_t, X_IDXS_FLOAT, left_y, left_z);
  fill_xyzt(road_edges[1], plan_t, X_IDXS_FLOAT, right_y, right_z);

  framed.setRoadEdgeStds({
    exp(edges.std.left[0].y),
    exp(edges.std.right[0].y),
  });
}

void fill_model(cereal::ModelDataV2::Builder &framed, const ModelOutput &net_outputs) {
  const auto &best_plan = net_outputs.plans.get_best_prediction();
  std::array<float, TRAJECTORY_SIZE> plan_t;
  std::fill_n(plan_t.data(), plan_t.size(), NAN);
  plan_t[0] = 0.0;
  for (int xidx=1, tidx=0; xidx<TRAJECTORY_SIZE; xidx++) {
    // increment tidx until we find an element that's further away than the current xidx
    for (int next_tid = tidx + 1; next_tid < TRAJECTORY_SIZE && best_plan.mean[next_tid].position.x < X_IDXS[xidx]; next_tid++) {
      tidx++;
    }
    if (tidx == TRAJECTORY_SIZE - 1) {
      // if the Plan doesn't extend far enough, set plan_t to the max value (10s), then break
      plan_t[xidx] = T_IDXS[TRAJECTORY_SIZE - 1];
      break;
    }

    // interpolate to find `t` for the current xidx
    float current_x_val = best_plan.mean[tidx].position.x;
    float next_x_val = best_plan.mean[tidx+1].position.x;
    float p = (X_IDXS[xidx] - current_x_val) / (next_x_val - current_x_val);
    plan_t[xidx] = p * T_IDXS[tidx+1] + (1 - p) * T_IDXS[tidx];
  }

  fill_plan(framed, best_plan);
  fill_lane_lines(framed, plan_t, net_outputs.lane_lines);
  fill_road_edges(framed, plan_t, net_outputs.road_edges);

  // meta
  fill_meta(framed.initMeta(), net_outputs.meta);

  // stop line, added by opkr
  fill_stop_line(framed.initStopLine(), net_outputs.stop_lines);

  // leads
  auto leads = framed.initLeadsV3(LEAD_MHP_SELECTION);
  std::array<float, LEAD_MHP_SELECTION> t_offsets = {0.0, 2.0, 4.0};
  for (int i=0; i<LEAD_MHP_SELECTION; i++) {
    fill_lead(leads[i], net_outputs.leads, i, t_offsets[i]);
  }
}

void model_publish(PubMaster &pm, uint32_t vipc_frame_id, uint32_t vipc_frame_id_extra, uint32_t frame_id, float frame_drop,
                   const ModelOutput &net_outputs, uint64_t timestamp_eof,
                   float model_execution_time, kj::ArrayPtr<const float> raw_pred, const bool valid) {
  const uint32_t frame_age = (frame_id > vipc_frame_id) ? (frame_id - vipc_frame_id) : 0;
  MessageBuilder msg;
  auto framed = msg.initEvent(valid).initModelV2();
  framed.setFrameId(vipc_frame_id);
  framed.setFrameIdExtra(vipc_frame_id_extra);
  framed.setFrameAge(frame_age);
  framed.setFrameDropPerc(frame_drop * 100);
  framed.setTimestampEof(timestamp_eof);
  framed.setModelExecutionTime(model_execution_time);
  if (send_raw_pred) {
    framed.setRawPredictions(raw_pred.asBytes());
  }
  fill_model(framed, net_outputs);
  pm.send("modelV2", msg);
}

void posenet_publish(PubMaster &pm, uint32_t vipc_frame_id, uint32_t vipc_dropped_frames,
                     const ModelOutput &net_outputs, uint64_t timestamp_eof, const bool valid) {
  MessageBuilder msg;
  const auto &v_mean = net_outputs.pose.velocity_mean;
  const auto &r_mean = net_outputs.pose.rotation_mean;
  const auto &v_std = net_outputs.pose.velocity_std;
  const auto &r_std = net_outputs.pose.rotation_std;

  auto posenetd = msg.initEvent(valid && (vipc_dropped_frames < 1)).initCameraOdometry();
  posenetd.setTrans({v_mean.x, v_mean.y, v_mean.z});
  posenetd.setRot({r_mean.x, r_mean.y, r_mean.z});
  posenetd.setTransStd({exp(v_std.x), exp(v_std.y), exp(v_std.z)});
  posenetd.setRotStd({exp(r_std.x), exp(r_std.y), exp(r_std.z)});

  posenetd.setTimestampEof(timestamp_eof);
  posenetd.setFrameId(vipc_frame_id);

  pm.send("cameraOdometry", msg);
}
