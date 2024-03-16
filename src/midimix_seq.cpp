#include "plugin.hpp"
#include <iostream>
#include <array>

struct mmseq_s : Module 
{
    rack::midi::Driver*         driver_{nullptr};
    rack::midi::InputQueue      input_{};
    rack::midi::InputDevice*    input_device_{nullptr};
    rack::midi::Output          output_{};
    rack::midi::OutputDevice*   output_device_{nullptr};
    int                         output_id_{-1},
                                input_id_{-1};
    int64_t                     frame_;
    std::array<bool, 24>        button_states_;

    mmseq_s() { onReset(); }
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
            if (in.getStatus() == 0x9)
            {
                uint8_t 
                    button = in.getNote() - 1,
                    button_state{}; 
                try { button_state = (button_states_.at(button) = !button_states_.at(button)); }
                catch(const std::exception& e) { std::cerr << e.what() << '\n'; }            
                setButton(button, button_state);
            }
        }
    }
};


struct mmseq_widget_s : ModuleWidget
{
    mmseq_widget_s(mmseq_s* module)
    {
        setModule(module);
        setPanel(createPanel(asset::plugin(instance, "res/midimix_seq.svg")));
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
