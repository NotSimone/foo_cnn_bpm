#include "pch.h"

#include "bpm.h"

#include <helpers/input_helpers.h>

namespace bpm
{

class calculate_bpm : public threaded_process_callback
{
  public:
    calculate_bpm(metadb_handle_list_cref items) : items(items){};

    void run(threaded_process_status &p_status, abort_callback &p_abort)
    {
        try
        {
            // TODO: Make this a configuration
            const std::string model_path = "./model";
            cppflow::model model(model_path, cppflow::model::TYPE::SAVED_MODEL);

            input_helper input;
            std::vector<float> buffer;
            for (t_size i = 0; i < items.get_size(); ++i)
            {
                p_abort.check();
                p_status.set_progress(i, items.get_size());
                p_status.set_item_path(items[i]->get_path());

                input.open(NULL, items[i], input_flag_no_looping | input_flag_no_postproc, p_abort);
                // Ignore the first 20% of the track
                // TODO: make this a configuration
                input.seek(items[i]->get_length() * 0.2, p_abort);

                audio_chunk_impl_temporary chunk;
                buffer.clear();

                // Load and resample the track
                while (input.run(chunk, p_abort))
                {
                    unsigned sample_rate = chunk.get_sample_rate();
                    t_size sample_count = chunk.get_sample_count();
                    t_size channel_count = chunk.get_channel_count();

                    // TODO: Support more sample rates
                    if (sample_rate != 44100)
                    {
                        throw std::exception("Bad sample rate");
                    }

                    // Sample is a unit of interleaved PCM data (ie 1 sample corresponds to 2 values for stereo PCM
                    // data, 6 for 5.1 etc)
                    sample_count = chunk.get_sample_count();
                    audio_sample *data_ptr = chunk.get_data();

                    // Downsample from 44.1k to 11.025k
                    for (t_size sample_num = 0; sample_num < sample_count; sample_num += channel_count * 4)
                    {
                        audio_sample sample = 0;

                        // Downmix if there are multiple channels
                        for (unsigned channel_num = 0; channel_num < channel_count; channel_num++)
                        {
                            sample += *(data_ptr + sample_num + channel_num);
                        }

                        sample /= channel_count;
                        buffer.push_back(static_cast<float>(sample));
                    }
                }

                p_status.set_progress_secondary_float(0.3);

                // Generate the input features
                constexpr int64_t bands = 40;
                constexpr int64_t frame_size = 256;
                // Output shape is [index, bands]
                std::vector<std::vector<float>> spectrogram = compute_mel_spectrogram(buffer, bands);

                buffer.clear();
                p_status.set_progress_secondary_float(0.4);
                normalise(buffer);
                p_status.set_progress_secondary_float(0.5);
                const int64_t n_frames = window(buffer, spectrogram, bands, frame_size);
                p_status.set_progress_secondary_float(0.6);

                // Run the model
                cppflow::tensor model_input = cppflow::tensor(buffer, {n_frames, bands, frame_size, 1});

                // TODO: Figure out whats going on with these input/output names
                auto output = model({{"serving_default_input_201:0", model_input}}, {"StatefulPartitionedCall:0"});
                p_status.set_progress_secondary_float(1.0);

                // [number_of_outputs, bpm-30]
                auto output_shape = output[0].shape().get_data<int64_t>();
                std::vector<float> results = output[0].get_data<float>();

                // TODO: Write the median to a tag instead of just printing
                std::stringstream out;
                for (auto begin_it = results.begin(); begin_it < results.end(); begin_it += output_shape[1])
                {
                    const size_t bpm =
                        std::distance(begin_it, std::max_element(begin_it, begin_it + output_shape[1])) + 30;
                    out << bpm << std::endl;
                }

                popup_message::g_show(out.str().c_str(), "BPM Analyser");
            }
        }
        catch (std::exception const &e)
        {
            fail_msg = e.what();
        }
    }

    void on_done(HWND p_wnd, bool p_was_aborted)
    {
        if (!p_was_aborted && core_api::assert_main_thread())
        {
            if (!fail_msg.is_empty())
            {
                popup_message::g_complain("BPM Analysis Failure", fail_msg);
            }
        }
    }

    std::vector<std::vector<float>> compute_mel_spectrogram(std::vector<float> input, int32_t bands) const
    {
        int32_t sr = 1105;
        int32_t n_fft = 1024;
        int32_t n_hop = 512;
        std::string window = "hann";
        bool center = true;
        std::string pad_mode = "reflect";
        float power = 1.0f;
        int32_t fmin = 20;
        int32_t fmax = 5000;

        return librosa::Feature::melspectrogram(input, sr, n_fft, n_hop, window, center, pad_mode, power, bands, fmin,
                                                fmax);
    }

    void normalise(std::vector<float> &data) const
    {
        const auto [mean, std_dev] = calculate_mean_and_std_dev(data);
        std::transform(std::execution::par, data.begin(), data.end(), data.begin(),
                       [mean, std_dev](float v) { return static_cast<float>((v - mean) / std_dev); });
    }

    std::pair<double, double> calculate_mean_and_std_dev(const std::vector<float> &input) const
    {
        const double count = static_cast<double>(input.size());
        const double mean = std::reduce(input.begin(), input.end()) / count;

        const double sum_square =
            std::transform_reduce(std::execution::par, input.begin(), input.end(), 0.0, std::plus<double>{},
                                  [mean](float v) { return std::pow(v - mean, 2); });
        const double std_dev = std::sqrt(sum_square / count);

        return {mean, std_dev};
    }

    int64_t window(std::vector<float> &output, const std::vector<std::vector<float>> &spectrogram, int64_t bands,
                   int64_t frame_size) const
    {
        constexpr int64_t hop_length = 128;
        // Generate windows
        int64_t n_frames = 0;
        for (size_t i = 0; i + frame_size < spectrogram.size(); i += hop_length)
        {
            for (int64_t j = 0; j < frame_size; ++j)
            {
                const std::vector<float> &sample = spectrogram[i + j];
                output.insert(output.end(), sample.begin(), sample.end());
            }
            ++n_frames;
        }

        return n_frames;
    }

  private:
    const metadb_handle_list items;
    pfc::string fail_msg;
};

void run_calculate_bpm(metadb_handle_list_cref data)
{
    service_ptr_t<threaded_process_callback> cb = new service_impl_t<calculate_bpm>(data);
    static_api_ptr_t<threaded_process>()->run_modeless(
        cb,
        threaded_process::flag_show_abort | threaded_process::flag_show_delayed | threaded_process::flag_show_minimize |
            threaded_process::flag_show_progress_dual | threaded_process::flag_show_item,
        core_api::get_main_window(), "Analysing BPMs...");
}

} // namespace bpm
