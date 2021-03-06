
#define TESLA_INIT_IMPL
#include <emuiibo.hpp>
#include <tesla.hpp>
#include <tesla_extensions.hpp>
#include <dirent.h>
#include <filesystem>
#include <fstream>
#include <set>
#include <upng.h>

namespace {
    enum Action : u64 {
        ShowHelp = KEY_PLUS,
        EnableEmulation = KEY_R,
        DisableEmulation = KEY_L,
        ActivateItem = KEY_A,
        AddToFavorite = KEY_Y,
        RemoveFromFavorite = KEY_X,
        ToogleConnectAmiibo = KEY_RSTICK,
        ResetActiveAmiibo = KEY_MINUS,
    };
    std::string actionGlyph(const Action action) {
        static const std::unordered_map<u64, std::string> KEY_GLYPH = {
            { KEY_RSTICK, "\uE0C5" },
            { KEY_LSTICK, "\uE0C4" },
            { KEY_L, "\uE0A4" },
            { KEY_R, "\uE0A5" },
            { KEY_A, "\uE0A0" },
            { KEY_Y, "\uE0A3" },
            { KEY_X, "\uE0A2" },
            { KEY_MINUS, "\uE0B4" },
            { KEY_PLUS, "\uE0B5" },
        };
        return KEY_GLYPH.at(action);
    }
    enum Icon {
        Help,
        Reset,
        Favorite,
    };
    std::string iconGlyph(const Icon icon) {
        static const std::unordered_map<Icon, std::string> KEY_GLYPH = {
            { Icon::Help, "\uE142" },
            { Icon::Reset, "\uE098" },
            { Icon::Favorite, "\u2605" },
        };
        return KEY_GLYPH.at(icon);
    }
    int marginIcon() {
        return 5;
    }
    int maxIconHeigth() {
        return 130 - 2 * marginIcon();
    }
    int maxIconWidth() {
        return tsl::cfg::LayerWidth / 2 - 2 * marginIcon();
    }
    std::string favoritesFile() {
        return "favorites.txt";
    }
}

class PngImage {

    private:
        std::filesystem::path path;
        bool is_error{false};
        std::string error_text{""};
        std::vector<u8> img_buffer{};
        int img_buffer_width{0};
        int img_buffer_height{0};

    public:
        PngImage() {
        }

        ~PngImage() {
            closeFile();
        }

        void openFile(const std::filesystem::path &png_path, const int max_height, const int max_width) {
            closeFile();
            path = png_path;
            tsl::hlp::doWithSDCardHandle([this, max_height, max_width] {
                upng_t* upng = upng_new_from_file(path.c_str());
                if (upng == NULL) {
                    setError("Bad file");
                    return;
                }
                upng_decode(upng);
                switch(upng_get_error(upng)) {
                    case UPNG_EOK: {
                        bool is_rgb = ( upng_get_format(upng) == UPNG_RGB8 || upng_get_format(upng) == UPNG_RGB16 );
                        /*
                        *  DELETE ONCE RGB DOWNSCALE WORKS PROPERLY
                        */
                        if (is_rgb){
                            setError("Please use RGBA PNG.");
                            break;
                        }
                        /* DELETE END */

                        int upng_width = upng_get_width(upng);
                        int upng_height = upng_get_height(upng);
                        int bpp = upng_get_bpp(upng);
                        int bitdepth = upng_get_bitdepth(upng);
                        int img_depth = bpp/bitdepth;
                        double scale1 = (double)max_height / (double)upng_height;
                        double scale2 = (double)max_width / (double)upng_width;
                        double scale = std::min(scale1, scale2);
                        if (scale > 1.0) {
                            setError("Upscale not allowed.");
                            break;
                        }

                        img_buffer_width = upng_width*scale;
                        img_buffer_height = upng_height*scale;
                        img_buffer.resize(img_buffer_width * img_buffer_height * img_depth);
                        std::fill(img_buffer.begin(), img_buffer.end(), 0);

                        /* DEBUG STRING START * /
                        std::string dbg_string;
                        if ( is_rgb ) {
                            dbg_string += "RGB - ";
                            //MAKE SOME MAGIC HERE TO PROPERLY DOWNSCALE RGB
                        } else {
                            dbg_string += "RGBA - ";
                        }
                        dbg_string += std::to_string(bpp) + "/" + std::to_string(bitdepth) + " ";
                        dbg_string += std::to_string(img_depth);
                        / * DEBUG STRING END */

                        for(int h = 0; h != img_buffer_height; ++h) {
                            for(int w = 0; w != img_buffer_width; ++w) {
                                int pixel = (h * (img_buffer_width *img_depth)) + (w*img_depth);
                                int nearestMatch =  (((int)(h / scale) * (upng_width *img_depth)) + ((int)(w / scale) *img_depth));
                                for(int d = 0; d != img_depth; ++d) {
                                    img_buffer[pixel + d] =  upng_get_buffer(upng)[nearestMatch + d];
                                }
                            }
                        }
                        break;
                    }
                    case UPNG_ENOMEM: {
                        setError("Image is too big.");
                        break;
                    }
                    case UPNG_ENOTFOUND: {
                        setError("Image not found.");
                        break;
                    }
                    case UPNG_ENOTPNG: {
                        setError("Image is not a PNG.");
                        break;
                    }
                    case UPNG_EMALFORMED: {
                        setError("PNG malformed.");
                        break;
                    }
                    case UPNG_EUNSUPPORTED: {
                        setError("This PNG not supported.");
                        break;
                    }
                    case UPNG_EUNINTERLACED: {
                        setError("Image interlacing is not supported.");
                        break;
                    }
                    case UPNG_EUNFORMAT: {
                        setError("Image color format is not supported.");
                        break;
                    }
                    case UPNG_EPARAM: {
                        setError("Invalid parameter.");
                        break;
                    }
                }
                upng_free(upng);
            });
        }

        void closeFile() {
            path.clear();
            error_text = {};
            is_error = false;
            img_buffer.clear();
            img_buffer_height = 0;
            img_buffer_width = 0;
        }

        const std::filesystem::path getPath() {
            return path;
        }

        const u8* getRGBABuffer() const {
            if (img_buffer.empty()) {
                return nullptr;
            }
            return img_buffer.data();
        }

        const int getHeight() const {
            return img_buffer_height;
        }

        const int getWidth() const {
            return img_buffer_width;
        }

        bool isError() const {
            return is_error;
        }

        std::string getError() const {
            return error_text;
        }

    private:

        void setError(const std::string &text) {
            closeFile();
            is_error = true;
            error_text = text;
        }
};

class EmuiiboState {

    private:
        bool Initialized{false};
        std::filesystem::path EmuiiboVirtualAmiiboDir;
        emu::Version Version;
        std::filesystem::path ActiveVirtualAmiiboPath;
        emu::VirtualAmiiboData ActiveVirtualAmiiboData;
        PngImage AmiiboImage;
        std::set<std::filesystem::path> favorites;

    public:
        bool isEmuiiboOk() const {
            return Initialized;
        }

        bool isCurrentApplicationIdIntercepted() const {
            return emu::IsCurrentApplicationIdIntercepted();
        }

        bool isActiveAmiiboValid() const {
            return !ActiveVirtualAmiiboPath.empty();
        }

        const emu::Version& getEmuiiboVersion() const {
            return Version;
        }

        std::string getEmuiiboVersionString() const {
            if (!isEmuiiboOk()) {
                return "emuiibo not found...";
            }
            const auto& Version = getEmuiiboVersion();
            return std::to_string(Version.major) + "." + std::to_string(Version.minor) + "." + std::to_string(Version.micro) + " (" + (Version.dev_build ? "dev" : "release") + ")";
        }

        emu::EmulationStatus getEmulationStatus() const {
            return emu::GetEmulationStatus();
        }

        std::string getEmuiiboVirtualAmiiboPath() const {
            return EmuiiboVirtualAmiiboDir;
        }

        std::string getActiveVirtualAmiiboPath() const {
            return ActiveVirtualAmiiboPath;
        }

        emu::VirtualAmiiboStatus getActiveVirtualAmiiboStatus() const {
            if(!isActiveAmiiboValid()) {
                return emu::VirtualAmiiboStatus::Invalid;
            }
            return emu::GetActiveVirtualAmiiboStatus();
        }

        const emu::VirtualAmiiboData& getActiveVirtualAmiiboAmiiboData() const {
            return ActiveVirtualAmiiboData;
        }

        bool getVirtualAmiiboAmiiboData(const std::string& path, emu::VirtualAmiiboData& data) const {
            return R_SUCCEEDED(emu::TryParseVirtualAmiibo(const_cast<char*>(path.c_str()), path.size(), &data));
        }

        const PngImage& image() const {
            return AmiiboImage;
        }

        void initEmuiibo() {
            tsl::hlp::doWithSmSession([this] {
                if(emu::IsAvailable()) {
                    Initialized = R_SUCCEEDED(emu::Initialize()) && R_SUCCEEDED(pmdmntInitialize()) && R_SUCCEEDED(pminfoInitialize());
                    if(Initialized) {
                        Version = emu::GetVersion();
                        char emuiibo_amiibo_dir_str[FS_MAX_PATH];
                        emu::GetVirtualAmiiboDirectory(emuiibo_amiibo_dir_str, FS_MAX_PATH);
                        EmuiiboVirtualAmiiboDir = std::string(emuiibo_amiibo_dir_str);
                    }
                }
            });
        }

        void setEmulationStatus(const emu::EmulationStatus status) {
            emu::SetEmulationStatus(status);
        }

        void toggleEmulationStatus() {
            switch(getEmulationStatus()) {
            case emu::EmulationStatus::On: {
                    setEmulationStatus(emu::EmulationStatus::Off);
                    break;
                }
            case emu::EmulationStatus::Off: {
                    setEmulationStatus(emu::EmulationStatus::On);
                    break;
                }
            }
        }

        void setActiveVirtualAmiibo(const std::string & path) {
            emu::SetActiveVirtualAmiibo(const_cast<char*>(path.c_str()), path.size());
            loadActiveAmiibo();
        }

        void ResetActiveVirtualAmiibo() {
            emu::ResetActiveVirtualAmiibo();
            loadActiveAmiibo();
        }

        void setActiveVirtualAmiiboStatus(const emu::VirtualAmiiboStatus status) {
            emu::SetActiveVirtualAmiiboStatus(status);
        }

        void toggleActiveVirtualAmiiboStatus() {
            switch(getActiveVirtualAmiiboStatus()) {
                case emu::VirtualAmiiboStatus::Connected: {
                    setActiveVirtualAmiiboStatus(emu::VirtualAmiiboStatus::Disconnected);
                    break;
                }
                case emu::VirtualAmiiboStatus::Disconnected: {
                    setActiveVirtualAmiiboStatus(emu::VirtualAmiiboStatus::Connected);
                    break;
                }
                case emu::VirtualAmiiboStatus::Invalid: {
                    break;
                }
            }
        }

        void loadActiveAmiibo() {
            char active_amiibo_path_str[FS_MAX_PATH];
            emu::GetActiveVirtualAmiibo(&ActiveVirtualAmiiboData, active_amiibo_path_str, FS_MAX_PATH);
            ActiveVirtualAmiiboPath = std::string(active_amiibo_path_str);

            AmiiboImage.closeFile();
            if(isActiveAmiiboValid()) {
                AmiiboImage.openFile(ActiveVirtualAmiiboPath / "amiibo.png", maxIconHeigth(), maxIconWidth());
            }
        }

        void loadFavorites() {
            favorites.clear();
            tsl::hlp::doWithSDCardHandle([this](){
                std::ifstream file(std::filesystem::path{getEmuiiboVirtualAmiiboPath()} / favoritesFile());
                std::string path_str;
                while (std::getline(file, path_str)) {
                    const std::filesystem::path path{path_str};
                    addToFavorite(getEmuiiboVirtualAmiiboPath() / path);
                }
            });
        }

        void saveFavorites() {
            tsl::hlp::doWithSDCardHandle([this](){
                std::ofstream file(std::filesystem::path{getEmuiiboVirtualAmiiboPath()} / favoritesFile(), std::ofstream::out | std::ofstream::trunc);
                for (const auto &path: favorites) {
                    file << path.lexically_relative(getEmuiiboVirtualAmiiboPath()).string() << std::endl;
                }
            });
        }

        std::list<std::filesystem::path> getFavorites() const {
            const std::list<std::filesystem::path> out{favorites.begin(), favorites.end()};
            return out;
        }

        void addToFavorite(const std::filesystem::path& path) {
            favorites.insert(path);
        }

        void removeFromFavorite(const std::filesystem::path& path) {
            favorites.erase(path);
        }

        bool isFavorite(const std::filesystem::path& path) const {
            return favorites.count(path) != 0;
        }
};

class GuiListElement: public tslext::elm::SmallListItem {
    private:
        std::shared_ptr<EmuiiboState> emuiibo;
        std::filesystem::path amiibo_path;
        std::function<void(GuiListElement&)> action_listener;

    public:
        GuiListElement(std::shared_ptr<EmuiiboState> state, const std::filesystem::path& path, const std::string& label, const std::string& value = {}) : tslext::elm::SmallListItem(label, value), emuiibo{state}, amiibo_path{path} {
            setClickListener([this] (u64 keys) {
                if(keys & Action::ActivateItem) {
                    action_listener(*this);
                }
                return false;
            });
        }

        void setActionListener(const std::function<void(GuiListElement&)>& listener) {
            action_listener = listener;
        }

        std::filesystem::path getPath() const {
            return amiibo_path;
        }

        bool isFavorite() const {
            return emuiibo->isFavorite(getPath());
        }

        void addToFavorite() {
            if (canBeFavorite()) {
                emuiibo->addToFavorite(getPath());
                update();
            }
        }

        void removeFromFavorite() {
            emuiibo->removeFromFavorite(getPath());
            update();
        }

        virtual bool canBeFavorite() const {
            return false;
        }

        virtual void update() {
        }
};

class VirtualListElement: public GuiListElement {
    public:
        VirtualListElement(std::shared_ptr<EmuiiboState> state, const std::string& label) : GuiListElement(state, {}, label, "..") {}
};

class ActionListElement: public GuiListElement {
    public:
        ActionListElement(std::shared_ptr<EmuiiboState> state, const std::string& label) : GuiListElement(state, {}, label, "") {}
};

class FolderListElement: public GuiListElement {
    public:
        FolderListElement(std::shared_ptr<EmuiiboState> state, const std::filesystem::path& path) : GuiListElement(state, path, path.filename()) {
            update();
        }

    private:
        void update() override {
            const std::string value = "..";
            setValue(isFavorite() ? iconGlyph(Icon::Favorite) + " " + value : value);
        }
};

class AmiiboListElement: public GuiListElement {
    public:
        AmiiboListElement(std::shared_ptr<EmuiiboState> state, const std::filesystem::path& path, const emu::VirtualAmiiboData& data) : GuiListElement(state, path, data.name) {
            update();
        }

    private:
        bool canBeFavorite() const {
            return true;
        }

        void update() override {
            const std::string value = actionGlyph(Action::ActivateItem);
            setValue(isFavorite() ? iconGlyph(Icon::Favorite) + " " + value : value);
        }
};

class AmiiboIcons: public tsl::elm::Element {

    private:
        std::shared_ptr<EmuiiboState> emuiibo;
        PngImage curent_amiibo_image;

    public:
        AmiiboIcons(std::shared_ptr<EmuiiboState> state) : emuiibo{state} {}

        void setCurrentAmiiboPath(std::filesystem::path amiibo_path) {
            if (amiibo_path.empty()) {
                curent_amiibo_image.closeFile();
                return;
            }
            if (curent_amiibo_image.getPath() != amiibo_path) {
                curent_amiibo_image.openFile(amiibo_path / "amiibo.png", maxIconHeigth(), maxIconWidth());
            }
        }

    private:
        virtual void draw(gfx::Renderer* renderer) override {
            renderer->enableScissoring(ELEMENT_BOUNDS(this));
            drawCustom(renderer, ELEMENT_BOUNDS(this));
            renderer->disableScissoring();
        }

        virtual void layout(u16 parentX, u16 parentY, u16 parentWidth, u16 parentHeight) override {
        }

        void drawIcon(tsl::gfx::Renderer* renderer, s32 x, s32 y, s32 w, s32 h, const PngImage& image) {
            const auto margin_icon = marginIcon();
            if(image.getRGBABuffer()){
                renderer->drawBitmap(x + margin_icon / 2 + w / 2 - image.getWidth() / 2,
                                     y + margin_icon,
                                     image.getWidth(), image.getHeight(), image.getRGBABuffer());
            } else {
                const auto font_size = 15;
                renderer->drawString(image.getError().c_str(), false,
                                     x + margin_icon,
                                     y + h / 2,
                                     font_size, renderer->a(tsl::style::color::ColorText));
            }
        }

        void drawCustom(tsl::gfx::Renderer* renderer, s32 x, s32 y, s32 w, s32 h) {
            const auto margin_icon = marginIcon();
            renderer->drawRect(x + w / 2 - 1, y, 1, h - margin_icon, a(tsl::style::color::ColorText));
            drawIcon(renderer, x, y, w / 2, h, emuiibo->image());
            drawIcon(renderer, x + w / 2, y, w / 2, h, curent_amiibo_image);
        }
};

class AmiiboGuiHelp : public tsl::Gui {

    private:
        std::shared_ptr<EmuiiboState> emuiibo;

    public:
        AmiiboGuiHelp(std::shared_ptr<EmuiiboState> state) : emuiibo{state} {}

        virtual tsl::elm::Element* createUI() override {
            auto root_frame = new tslext::elm::DoubleSectionOverlayFrame("emuiibo help", emuiibo->getEmuiiboVersionString(), tslext::SectionsLayout::big_top, false);
            auto top_list = new tsl::elm::List();
            root_frame->setTopSection(top_list);

            top_list->addItem(new tslext::elm::SmallListItem("Help", actionGlyph(Action::ShowHelp)));
            top_list->addItem(new tslext::elm::SmallListItem("Enable emulation", actionGlyph(Action::EnableEmulation)));
            top_list->addItem(new tslext::elm::SmallListItem("Disable emulation", actionGlyph(Action::DisableEmulation)));
            top_list->addItem(new tslext::elm::SmallListItem("Connect/disconnect virtual amiibo", actionGlyph(Action::ToogleConnectAmiibo)));
            top_list->addItem(new tslext::elm::SmallListItem("Select folder/virtual amiibo", actionGlyph(Action::ActivateItem)));
            top_list->addItem(new tslext::elm::SmallListItem("Add to favorites", actionGlyph(Action::AddToFavorite)));
            top_list->addItem(new tslext::elm::SmallListItem("Remove from favorites", actionGlyph(Action::RemoveFromFavorite)));
            top_list->addItem(new tslext::elm::SmallListItem("Reset active amiibo", actionGlyph(Action::ResetActiveAmiibo)));

            return root_frame;
        }
};

class AmiiboGui : public tsl::Gui {

    public:
        enum class Type {
            Root,
            Favorites,
            Folder,
        };

    private:
        std::shared_ptr<EmuiiboState> emuiibo;
        const Type gui_type;
        const std::filesystem::path base_path;
        tslext::elm::DoubleSectionOverlayFrame *root_frame{nullptr};
        tslext::elm::SmallToggleListItem *toggle_item{nullptr};
        tslext::elm::SmallListItem *game_header{nullptr};
        tslext::elm::SmallListItem *amiibo_header{nullptr};
        AmiiboIcons* amiibo_icons;
        tsl::elm::List *top_list{nullptr};
        tsl::elm::List *bottom_list{nullptr};

    public:
        AmiiboGui(std::shared_ptr<EmuiiboState> state, const Type type, const std::filesystem::path &path) : emuiibo{state}, gui_type{type}, base_path(path) {}

        virtual tsl::elm::Element *createUI() override {
            // View frame with 2 section
            root_frame = new tslext::elm::DoubleSectionOverlayFrame("emuiibo", emuiibo->getEmuiiboVersionString(), tslext::SectionsLayout::same, true);

            // Top and bottom containers
            top_list = new tsl::elm::List();
            root_frame->setTopSection(top_list);
            bottom_list = new tsl::elm::List();
            root_frame->setBottomSection(bottom_list);

            if(!emuiibo->isEmuiiboOk()) {
                return root_frame;
            }

            // Iterate base folder
            u32 amiibo_count = 0;
            if (gui_type == Type::Root) {
                bottom_list->addItem(createRootElement());
                bottom_list->addItem(createFavoritesElement());
                bottom_list->addItem(createResetElement());
                bottom_list->addItem(createHelpElement());
            }
            else {
                std::list<std::filesystem::path> dir_paths;
                if (gui_type == Type::Favorites) {
                    dir_paths = emuiibo->getFavorites();
                }
                if (gui_type == Type::Folder) {
                    tsl::hlp::doWithSDCardHandle([base_path = base_path, &dir_paths](){
                        auto dir = opendir(base_path.c_str());
                        if(dir) {
                            while(true) {
                                auto entry = readdir(dir);
                                if(entry == nullptr) {
                                    break;
                                }
                                if(!(entry->d_type & DT_DIR)) {
                                    continue;
                                }
                                const auto dir_path = base_path / entry->d_name;
                                dir_paths.push_back(dir_path);
                            }
                            closedir(dir);
                        }
                    });
                }
                for (const auto &dir_path: dir_paths) {
                    if (tsl::elm::Element* item = createAmiiboElement(dir_path)) {
                        bottom_list->addItem(item);
                        amiibo_count++;
                        continue;
                    }
                    if (tsl::elm::Element* item = createFolderElement(dir_path)) {
                        bottom_list->addItem(item);
                    }
                }
            }

            // emuiibo emulation status
            toggle_item = new tslext::elm::SmallToggleListItem("Emulation status " + actionGlyph(Action::DisableEmulation) + " " + actionGlyph(Action::EnableEmulation), false, "on", "off");
            toggle_item->setClickListener([&](u64 keys) {
                if(keys & Action::ActivateItem){
                    emuiibo->toggleEmulationStatus();
                    return true;
                }
                return false;
            });
            top_list->addItem(toggle_item);

            // Current game status
            game_header = new tslext::elm::SmallListItem("Current game is");
            top_list->addItem(game_header);

            // Current amiibo
            amiibo_header = new tslext::elm::SmallListItem("");
            top_list->addItem(amiibo_header);

            // Current amiibo icon
            amiibo_icons = new AmiiboIcons(emuiibo);
            top_list->addItem(amiibo_icons, maxIconHeigth() + 2 * marginIcon());

            // Information about base folder
            top_list->addItem(new tslext::elm::SmallListItem(std::string("Available amiibos in '") + base_path.filename().string() + "'", std::to_string(amiibo_count)));

            // Main key bindings
            root_frame->setClickListener([&](u64 keys) {
                if(keys & Action::ShowHelp) {
                    tsl::changeTo<AmiiboGuiHelp>(emuiibo);
                    return true;
                }
                if(keys & Action::ToogleConnectAmiibo) {
                    emuiibo->toggleActiveVirtualAmiiboStatus();
                    return true;
                }
                if(keys & Action::EnableEmulation) {
                    emuiibo->setEmulationStatus(emu::EmulationStatus::On);
                    return true;
                }
                if(keys & Action::DisableEmulation) {
                    emuiibo->setEmulationStatus(emu::EmulationStatus::Off);
                    return true;
                }
                if(keys & Action::ResetActiveAmiibo) {
                    emuiibo->ResetActiveVirtualAmiibo();
                    return true;
                }
                if (auto* gui_item = dynamic_cast<GuiListElement*>(getFocusedElement())) {
                    if (keys & Action::AddToFavorite) {
                        gui_item->addToFavorite();
                        return true;
                    }
                    if (keys & Action::RemoveFromFavorite) {
                        gui_item->removeFromFavorite();
                        return true;
                    }
                }
                return false;
            });

            update();

            return root_frame;
        }

        virtual void update() override {
            if(!emuiibo->isEmuiiboOk()) {
                return;
            }

            game_header->setColoredValue(emuiibo->isCurrentApplicationIdIntercepted() ? "intercepted" : "not intercepted",
                                         emuiibo->isCurrentApplicationIdIntercepted() ? tsl::style::color::ColorHighlight : tslext::style::color::ColorWarning);

            if(emuiibo->isActiveAmiiboValid()) {
                amiibo_header->setText(std::string(emuiibo->getActiveVirtualAmiiboAmiiboData().name) + " " + actionGlyph(Action::ToogleConnectAmiibo));
            }
            else {
                amiibo_header->setText("No active virtual amiibo");
            }
            amiibo_header->setColoredValue(emuiibo->getActiveVirtualAmiiboStatus() == emu::VirtualAmiiboStatus::Connected ? "connected" : "disconnected",
                                           emuiibo->getActiveVirtualAmiiboStatus() == emu::VirtualAmiiboStatus::Connected ? tsl::style::color::ColorHighlight : tslext::style::color::ColorWarning);

            if (auto* amiibo_item = dynamic_cast<AmiiboListElement*>(getFocusedElement())) {
                amiibo_icons->setCurrentAmiiboPath(amiibo_item->getPath());
            }
            else {
                amiibo_icons->setCurrentAmiiboPath({});
            }

            toggle_item->setState(emuiibo->getEmulationStatus() == emu::EmulationStatus::On ? true : false);

            tsl::Gui::update();
        }

    private:
        tsl::elm::Element* createRootElement() {
            auto item = new VirtualListElement(emuiibo, "View amiibos");
            item->setActionListener([this] (auto&) {
                tsl::changeTo<AmiiboGui>(emuiibo, Type::Folder, emuiibo->getEmuiiboVirtualAmiiboPath());
            });
            return item;
        }

        tsl::elm::Element* createFavoritesElement() {
            auto item = new VirtualListElement(emuiibo, "Favorites " + iconGlyph(Icon::Favorite));
            item->setActionListener([this](auto&) {
                tsl::changeTo<AmiiboGui>(emuiibo, Type::Favorites, "<favorites>");
            });
            return item;
        }

        tsl::elm::Element* createResetElement() {
            auto item = new ActionListElement(emuiibo, "Reset active " + iconGlyph(Icon::Reset));
            item->setActionListener([this](auto&) {
                emuiibo->ResetActiveVirtualAmiibo();
                update();
            });
            return item;
        }

        tsl::elm::Element* createHelpElement() {
            auto item = new ActionListElement(emuiibo, "Help " + iconGlyph(Icon::Help));
            item->setActionListener([this](auto&) {
                tsl::changeTo<AmiiboGuiHelp>(emuiibo);;
            });
            return item;
        }

        tsl::elm::Element* createFolderElement(const std::filesystem::path& path) {
            auto item = new FolderListElement(emuiibo, path);
            item->setActionListener([this](auto& caller) {
                tsl::changeTo<AmiiboGui>(emuiibo, Type::Folder, caller.getPath());
            });
            return item;
        }

        tsl::elm::Element* createAmiiboElement(const std::filesystem::path& path) {
            emu::VirtualAmiiboData data = {};
            if(!emuiibo->getVirtualAmiiboAmiiboData(path, data)) {
                return nullptr;
            }
            auto item = new AmiiboListElement(emuiibo, path, data);
            item->setActionListener([this](auto& caller) {
                if (emuiibo->getActiveVirtualAmiiboPath() != caller.getPath()) {
                    emuiibo->setActiveVirtualAmiibo(caller.getPath());
                }
                else {
                    emuiibo->toggleActiveVirtualAmiiboStatus();
                }
            });
            return item;
        }
};

class EmuiiboOverlay : public tsl::Overlay {
    private:
        std::shared_ptr<EmuiiboState> emuiibo;

    public:
        EmuiiboOverlay() {
            emuiibo = std::make_shared<EmuiiboState>();
        }

        virtual void initServices() override {
            emuiibo->initEmuiibo();
        }

        virtual void exitServices() override {
            emuiibo->saveFavorites();
            pminfoExit();
            pmdmntExit();
            emu::Exit();
        }

        virtual std::unique_ptr<tsl::Gui> loadInitialGui() override {
            emuiibo->loadActiveAmiibo();
            emuiibo->loadFavorites();
            return initially<AmiiboGui>(emuiibo, AmiiboGui::Type::Root, "<root>");
        }
};

int main(int argc, char **argv) {
    return tsl::loop<EmuiiboOverlay>(argc, argv);
}
