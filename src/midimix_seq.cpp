#include "plugin.hpp"
#include <array>
#include <chrono>
#include <iostream>

#define MM_CC_MAX       63
#define MM_NOTE_MAX     25

namespace
{
    constexpr
    std::array<uint8_t, 8>  button_mapping1 = {1 + 0, 4 + 0, 7 + 0, 10 + 0, 13 + 0, 16 + 0, 19 + 0, 22 + 0},
                            button_mapping2 = {1 + 1, 4 + 1, 7 + 1, 10 + 1, 13 + 1, 16 + 1, 19 + 1, 22 + 1},
                            button_mapping3 = {1 + 2, 4 + 2, 7 + 2, 10 + 2, 13 + 2, 16 + 2, 19 + 2, 22 + 2},
                            knob_mapping1   = {16 + 0, 20 + 0, 24 + 0, 28 + 0, 46 + 0, 50 + 0, 54 + 0, 58 + 0},
                            knob_mapping2   = {16 + 1, 20 + 1, 24 + 1, 28 + 1, 46 + 1, 50 + 1, 54 + 1, 58 + 1},
                            knob_mapping3   = {16 + 2, 20 + 2, 24 + 2, 28 + 2, 46 + 2, 50 + 2, 54 + 2, 58 + 2},
                            fader_mapping   = {16 + 3, 20 + 3, 24 + 3, 28 + 3, 46 + 3, 50 + 3, 54 + 3, 58 + 3};
}

struct mmseq_s : Module
{
    enum PARAM_IDS
    {
        NUM_PARAMS
    };
    enum INPUT_IDS
    {
        CLOCK_INPUT,
        RESET_INPUT1,
        RESET_INPUT2,
        RESET_INPUT3,
        NUM_INPUTS
    };
    enum OUTPUT_IDS
    {
        CV_OUT1,
        CV_OUT2,
        CV_OUT3,
        GATE_OUT1,
        GATE_OUT2,
        GATE_OUT3,
        NUM_OUTPUTS
    };
    enum LIGHT_IDS
    {
        NUM_LIGHTS
    };

    using trigger_v = std::array<dsp::SchmittTrigger, 3>;
    using pulse_v   = std::array<dsp::PulseGenerator, 3>;

    std::array<bool, MM_NOTE_MAX>   button_states_          {};
    std::array<uint8_t, MM_CC_MAX>  knob_vals_              {};
    rack::midi::Driver*             driver_                 {nullptr};
    rack::midi::InputQueue          input_                  {};
    rack::midi::InputDevice*        input_device_           {nullptr};
    rack::midi::Output              output_                 {};
    rack::midi::OutputDevice*       output_device_          {nullptr};
    int64_t                         frame_                  {0};
    int                             output_id_              {-1},
                                    input_id_               {-1};
    std::array<uint8_t, 3>          seq_positions_          {0, 0, 0},
                                    seq_lengths_            {8, 8 ,8};
    dsp::SchmittTrigger             clock_trigger_          {};
    trigger_v                       reset_triggers_         {};
    pulse_v                         gate_pulses_            {};

    mmseq_s()
    {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        configInput(CLOCK_INPUT, "Clock");
        configInput(RESET_INPUT1, "Reset1");
        configInput(RESET_INPUT2, "Reset2");
        configInput(RESET_INPUT3, "Reset3");
        configOutput(GATE_OUT1, "Gate1");
        configOutput(GATE_OUT2, "Gate2");
        configOutput(GATE_OUT3, "Gate3");
        configOutput(CV_OUT1, "Cv1");
        configOutput(CV_OUT2, "Cv2");
        configOutput(CV_OUT3, "Cv3");
        onReset();
    }
    ~mmseq_s()
    {
        rack::midi::Message in;
        while (input_.tryPop(&in, frame_)) {};
    }
    void setButton(uint8_t button, uint8_t button_state)
    {
        if (!output_device_) return;
        rack::midi::Message out;
        out.setStatus(0x9);
        out.setNote(button);
        out.setValue(button_state);
        out.setFrame(frame_);
        output_device_->sendMessage(out);
    }
    void syncButtons()
    {
        for(size_t i = 0; i < button_states_.size(); ++i)
        {
            setButton(i, button_states_.at(i));
        }
    }
    void onReset() override { output_.reset(); }
    void process(const ProcessArgs& args) override
    {
        frame_ = args.frame;
        rack::midi::Message in;
        while (input_.tryPop(&in, frame_))
        {
            switch (in.getStatus())
            {
                case 0x9:
                {
                    uint8_t
                        button = in.getNote(),
                        button_state{};
                    try { button_state = (button_states_.at(button) = !button_states_.at(button)); }
                    catch(const std::exception& e) { std::cerr << e.what() << '\n'; }
                    setButton(button, button_state);
                    break;
                }
                case 0xb:
                {
                    uint8_t
                        knob = in.getNote(),
                        knob_val = in.getValue();
                    try { knob_vals_.at(knob) = knob_val; }
                    catch(const std::exception& e) { std::cerr << e.what() << '\n'; }
                    break;
                }
                default: break;
            }
        }

        auto process_sequencer =
        [this, &args](
            size_t seq_nr,
            size_t reset_in,
            size_t gate_out,
            size_t cv_out,
            bool advance,
            const std::array<uint8_t, 8> &button_map,
            const std::array<uint8_t, 8> &knob_map
        ){
            try
            {
                bool reset  = reset_triggers_.at(seq_nr).process(inputs[reset_in].getVoltage(), 0.1f, 2.f);
                uint8_t
                    previous_step = button_map.at(seq_positions_.at(seq_nr)),
                    current_button{},
                    current_knob{};

                seq_positions_.at(seq_nr)    =  (uint8_t) (seq_positions_.at(seq_nr) + advance)
                                                % (seq_lengths_.at(seq_nr) ? seq_lengths_.at(seq_nr) : 1);
                seq_positions_.at(seq_nr)    =  reset ? 0 : seq_positions_.at(seq_nr);
                current_button               =  button_map.at(seq_positions_.at(seq_nr));
                current_knob                 =  knob_map.at(seq_positions_.at(seq_nr));

                if (advance || reset)
                {
                    if (button_states_.at(current_button)) gate_pulses_.at(seq_nr).trigger(1e-3f);
                    setButton(previous_step, button_states_.at(previous_step));
                    setButton(current_button, !button_states_.at(current_button));
                }
                auto gate = gate_pulses_.at(seq_nr).process(args.sampleTime);
                outputs[gate_out].setVoltage(gate ? 10.f : 0.f);
                outputs[cv_out].setVoltage(knob_vals_.at(current_knob) / 127.0f * 10.0f);
            }
            catch(const std::exception& e) { std::cerr << e.what() << '\n'; }
        };
        bool advance = clock_trigger_.process(inputs[CLOCK_INPUT].getVoltage(), 0.1f, 2.f);
        process_sequencer(0, RESET_INPUT1, GATE_OUT1, CV_OUT1, advance, button_mapping1, knob_mapping1);
        process_sequencer(1, RESET_INPUT2, GATE_OUT2, CV_OUT2, advance, button_mapping2, knob_mapping2);
        process_sequencer(2, RESET_INPUT3, GATE_OUT3, CV_OUT3, advance, button_mapping3, knob_mapping3);
    }
};


struct mmseq_widget_s : ModuleWidget
{
    mmseq_widget_s(mmseq_s* module)
    {

        setModule(module);
        setPanel(createPanel(asset::plugin(instance, "res/midimix_seq.svg")));
        auto add_input =
        [this, &module]
        (const bool left_aligned, const int row, const int enumeration)
        {
            const int
                left = 5,
                right = 25,
                spacer = 20,
                offset = 20;
            addInput(createInputCentered<ThemedPJ301MPort>(
                mm2px(Vec(left_aligned ? left : right, offset + spacer * row)),
                module,
                enumeration));
        };
        auto add_output =
        [this, &module]
        (const bool left_aligned, const int row, const int enumeration)
        {
            const int
                left = 5,
                right = 25,
                spacer = 20,
                offset = 20;
            addOutput(createOutputCentered<ThemedPJ301MPort>(
                mm2px(Vec(left_aligned ? left : right, offset + spacer * row)),
                module,
                enumeration));
        };
        #define left true
        #define right false
        add_input(  left,    0, mmseq_s::CLOCK_INPUT);
        add_input(  left,    1, mmseq_s::RESET_INPUT2);
        add_input(  right,   0, mmseq_s::RESET_INPUT1);
        add_input(  right,   1, mmseq_s::RESET_INPUT3);
        add_output( left,    2, mmseq_s::GATE_OUT1);
        add_output( left,    3, mmseq_s::GATE_OUT2);
        add_output( left,    4, mmseq_s::GATE_OUT3);
        add_output( right,   2, mmseq_s::CV_OUT1);
        add_output( right,   3, mmseq_s::CV_OUT2);
        add_output( right,   4, mmseq_s::CV_OUT3);
    }
    void appendContextMenu(Menu* menu) override
    {
        mmseq_s* mmseq = getModule<mmseq_s>();
        menu->addChild(createSubmenuItem(
            "Midi in", "",
            [=](Menu* menu)
            {
                if (mmseq->driver_)
                {
                    menu->addChild(createMenuLabel(mmseq->driver_->getName()));
                    for (const auto &id : mmseq->driver_->getInputDeviceIds())
                    {
                        menu->addChild(createCheckMenuItem(
                            mmseq->driver_->getInputDeviceName(id), "",
                            [mmseq, id]() { return mmseq->input_id_ ? id == mmseq->input_id_ : false; },
                            [mmseq, id]()
                            {
                                mmseq->driver_->unsubscribeInput(mmseq->input_id_, &mmseq->input_);
                                mmseq->input_id_ = id;
                                mmseq->input_device_ = mmseq->driver_->subscribeInput(id, &mmseq->input_);
                            }
                        ));
                    }
                }
            }
        ));
        menu->addChild(createSubmenuItem(
            "Midi out", "",
            [=](Menu* menu)
            {
                if (mmseq->driver_)
                {
                    menu->addChild(createMenuLabel(mmseq->driver_->getName()));
                    for (const auto &id : mmseq->driver_->getOutputDeviceIds())
                    {
                        menu->addChild(createCheckMenuItem(
                            mmseq->driver_->getOutputDeviceName(id), "",
                            [mmseq, id]() { return mmseq->output_id_ ? id == mmseq->output_id_ : false; },
                            [mmseq, id]()
                            {
                                mmseq->driver_->unsubscribeOutput(mmseq->output_id_, &mmseq->output_);
                                mmseq->output_id_ = id;
                                mmseq->output_device_ = mmseq->driver_->subscribeOutput(id, &mmseq->output_);
                                mmseq->syncButtons();
                            }
                        ));
                    }
                }
            }
        ));
        menu->addChild(createSubmenuItem(
            "Midi drivers", "",
            [=](Menu* menu)
            {
                for (const auto &id : rack::midi::getDriverIds())
                {
                    rack::midi::Driver* driver = rack::midi::getDriver(id);
                    menu->addChild(createCheckMenuItem(
                        driver->getName(), "",
                        [mmseq, driver]() { return mmseq->driver_ ? mmseq->driver_->getName() == driver->getName() : false; },
                        [mmseq, driver]() { mmseq->driver_ = driver; }
                    ));
                }
            }
        ));
    }
};

Model* model = createModel<mmseq_s, mmseq_widget_s>("midimix_seq");
