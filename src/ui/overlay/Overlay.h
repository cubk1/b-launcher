#pragma once

class Overlay {
public:
    Overlay* host;
    bool marked;
    float markX;
    float markY;
    float markW;
    float markH;
    bool shown;
    float shownX;
    float shownY;
    float shownW;
    float shownH;

    Overlay();
    virtual ~Overlay() = default;

    virtual bool isOpen();
    virtual void release();
    virtual void draw();
    virtual bool scroll(double amount);
    virtual bool stepBack();
    virtual bool bounded();
    virtual bool contains(float x, float y);
    void mark(float x, float y, float w, float h);
    void clearMark();
    bool shownAt(float x, float y) const;
};

class SubOverlay final : public Overlay {
public:
    bool isOpen() override;
    void release() override;
    void draw() override;
    bool stepBack() override;
    bool scroll(double amount) override;
};

class MenuOverlay final : public Overlay {
public:
    bool isOpen() override;
    void release() override;
    void draw() override;
};

class PopupOverlay final : public Overlay {
public:
    bool isOpen() override;
    void release() override;
    void draw() override;
    bool scroll(double amount) override;
};

class PickerOverlay final : public Overlay {
public:
    bool isOpen() override;
    void release() override;
    void draw() override;
    bool scroll(double amount) override;
};

class SwatchOverlay final : public Overlay {
public:
    bool isOpen() override;
    void release() override;
    void draw() override;
};

class FieldMenuOverlay final : public Overlay {
public:
    bool isOpen() override;
    void release() override;
    void draw() override;
};
