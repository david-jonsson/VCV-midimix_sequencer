#include "plugin.hpp"
#include <iostream>
#include <array>

#define MM_CC_OFFSET    16
#define MM_CC_TOT       63 - 16
#define MM_NOTE_MAX     24

struct mmseq_s : Module
{
    enum PARAM_IDS
    {
        NUM_PARAMS
    };
    enum INPUT_IDS
    {
        CLOCK_INPUT,
        RESET_INPUT,
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

    rack::midi::Driver*             driver_{nullptr};
    rack::midi::InputQueue          input_{};
    rack::midi::InputDevice*        input_device_{nullptr};
    rack::midi::Output              output_{};
    rack::midi::OutputDevice*       output_device_{nullptr};
    int                             output_id_{-1},
                                    input_id_{-1};
    int64_t                         frame_{0};
    std::array<bool, MM_NOTE_MAX>   button_states_{false};
    std::array<uint8_t, MM_CC_TOT>  knob_vals_{0};
    mmseq_s()
    {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        configInput(CLOCK_INPUT, "Clock");
        configInput(RESET_INPUT, "Reset");
        configOutput(GATE_OUT1, "Gate1");
        configOutput(GATE_OUT2, "Gate2");
        configOutput(GATE_OUT3, "Gate3");
        configOutput(CV_OUT1, "Cv1");
        configOutput(CV_OUT2, "Cv2");
        configOutput(CV_OUT3, "Cv3");
        onReset();
    }
    void setButton(uint8_t button, uint8_t button_state)
    {
        rack::midi::Message out;
        out.setStatus(0x9);
        out.setNote(button + 1);
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
            std::cout << in.toString() << std::endl;
            switch (in.getStatus()) 
            {
                case 0x9:
                {
                    uint8_t
                        button = in.getNote() - 1,
                        button_state{};
                    try { button_state = (button_states_.at(button) = !button_states_.at(button)); }
                    catch(const std::exception& e) { std::cerr << e.what() << '\n'; }
                    setButton(button, button_state);
                    break;
                }
                case 0xb:
                {
                    uint8_t
                        knob = in.getNote() - MM_CC_OFFSET,
                        knob_val = in.getValue();
                    try { knob_vals_.at(knob) = knob_val; }
                    catch(const std::exception& e) { std::cerr << e.what() << '\n'; }
                    break;
                }
                default: break;
            }
        }
    }
};


struct mmseq_widget_s : ModuleWidget
{
    mmseq_widget_s(mmseq_s* module)
    {
        const int
            left = 5,
            right = 25,
            spacer = 25,
            offset = 15;
        setModule(module);
        setPanel(createPanel(asset::plugin(instance, "res/midimix_seq.svg")));
        addInput(createInputCentered<ThemedPJ301MPort>(mm2px(Vec(left, offset)), module, mmseq_s::CLOCK_INPUT));
        addInput(createInputCentered<ThemedPJ301MPort>(mm2px(Vec(right, offset)), module, mmseq_s::RESET_INPUT));
        addOutput(createOutputCentered<ThemedPJ301MPort>(mm2px(Vec(left, offset + spacer * 1)), module, mmseq_s::GATE_OUT1));
        addOutput(createOutputCentered<ThemedPJ301MPort>(mm2px(Vec(left, offset + spacer * 2)), module, mmseq_s::GATE_OUT2));
        addOutput(createOutputCentered<ThemedPJ301MPort>(mm2px(Vec(left, offset + spacer * 3)), module, mmseq_s::GATE_OUT3));
        addOutput(createOutputCentered<ThemedPJ301MPort>(mm2px(Vec(right, offset + spacer * 1)), module, mmseq_s::CV_OUT1));
        addOutput(createOutputCentered<ThemedPJ301MPort>(mm2px(Vec(right, offset + spacer * 2)), module, mmseq_s::CV_OUT2));
        addOutput(createOutputCentered<ThemedPJ301MPort>(mm2px(Vec(right, offset + spacer * 3)), module, mmseq_s::CV_OUT3));
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
