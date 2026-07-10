#include "tags_module_impl.h"
#include "tags_module_detail.h"

namespace {

std::string LowerAsciiForTagIndex(std::string_view value) {
    std::string lowered;
    lowered.reserve(value.size());
    for (const unsigned char ch : value) {
        lowered.push_back(static_cast<char>(std::tolower(ch)));
    }
    return lowered;
}

} // namespace

void TagsModule::Impl::TagRegistry::Clear() {
    entries_.clear();
    simpleIndex_.clear();
    functionIndex_.clear();
}

void TagsModule::Impl::TagRegistry::RegisterSimple(
    std::string name,
    std::string token,
    std::string example,
    UiText descriptionText,
    TagEntry::SimpleResolver resolver) {
    const std::size_t index = entries_.size();
    simpleIndex_[LowerAsciiForTagIndex(name)] = index;
    entries_.push_back(TagEntry{
        TagKind::Simple,
        std::move(name),
        std::move(token),
        std::move(example),
        descriptionText,
        std::move(resolver),
        {},
    });
}

void TagsModule::Impl::TagRegistry::RegisterFunction(
    std::string name,
    std::string token,
    std::string example,
    UiText descriptionText,
    TagEntry::FunctionResolver resolver) {
    const std::size_t index = entries_.size();
    functionIndex_[LowerAsciiForTagIndex(name)] = index;
    entries_.push_back(TagEntry{
        TagKind::Function,
        std::move(name),
        std::move(token),
        std::move(example),
        descriptionText,
        {},
        std::move(resolver),
    });
}

const std::vector<TagsModule::Impl::TagEntry>& TagsModule::Impl::TagRegistry::Entries() const {
    return entries_;
}

const TagsModule::Impl::TagEntry* TagsModule::Impl::TagRegistry::Find(TagKind kind, std::string_view name) const {
    const auto& index = kind == TagKind::Simple ? simpleIndex_ : functionIndex_;
    const auto it = index.find(std::string(name));
    if (it == index.end() || it->second >= entries_.size()) {
        return nullptr;
    }
    return &entries_[it->second];
}

const TagsModule::Impl::TagEntry* TagsModule::Impl::TagRegistry::FindByIndex(int index) const {
    if (index < 0 || index >= static_cast<int>(entries_.size())) {
        return nullptr;
    }
    return &entries_[static_cast<std::size_t>(index)];
}

std::size_t TagsModule::Impl::TagRegistry::Count(TagKind kind) const {
    std::size_t count = 0;
    for (const TagEntry& entry : entries_) {
        if (entry.kind == kind) {
            ++count;
        }
    }
    return count;
}

void TagsModule::Impl::RefreshCatalogEntries() {
    catalogEntries_.clear();
    catalogEntries_.reserve(tagRegistry_.Entries().size());
    for (const TagEntry& entry : tagRegistry_.Entries()) {
        catalogEntries_.push_back(CatalogEntry{
            entry.kind,
            entry.name,
            entry.token,
            entry.example,
            entry.descriptionText,
        });
    }
    ++catalogEntriesRevision_;
    InvalidateVariablePickerEntriesCache();
}

void TagsModule::Impl::InitializeRegistry() {
    tagRegistry_.Clear();

    tagRegistry_.RegisterSimple(
        "id",
        "{id}",
        "{id}",
        UiText::TagsBuiltinIdDescription,
        [](const Impl& module, const EvaluationContext& context) {
            return module.ResolveBuiltinIdTag(context);
        });

    tagRegistry_.RegisterSimple(
        "nick",
        "{nick}",
        "{nick}",
        UiText::TagsBuiltinNickDescription,
        [](const Impl& module, const EvaluationContext& context) {
            return module.ResolveBuiltinNickTag(context);
        });

    tagRegistry_.RegisterSimple(
        "thisbind",
        "{thisbind}",
        "{thisbind}",
        UiText::TagsBuiltinThisbindDescription,
        [](const Impl& module, const EvaluationContext& context) {
            return module.ResolveBuiltinThisbindTag(context);
        });

    tagRegistry_.RegisterSimple(
        "thisbindselector",
        "{thisbindselector}",
        "{thisbindselector}",
        UiText::TagsBuiltinThisbindSelectorDescription,
        [](const Impl& module, const EvaluationContext& context) {
            return module.ResolveBuiltinThisbindTag(context);
        });

    tagRegistry_.RegisterSimple(
        "thisbindname",
        "{thisbindname}",
        "{thisbindname}",
        UiText::TagsBuiltinThisbindNameDescription,
        [](const Impl& module, const EvaluationContext& context) {
            return module.ResolveBuiltinThisbindNameTag(context);
        });

    tagRegistry_.RegisterSimple(
        "thisbindfolder",
        "{thisbindfolder}",
        "{thisbindfolder}",
        UiText::TagsBuiltinThisbindFolderDescription,
        [](const Impl& module, const EvaluationContext& context) {
            return module.ResolveBuiltinThisbindFolderTag(context);
        });

    tagRegistry_.RegisterSimple(
        "thiscategory",
        "{thiscategory}",
        "{thiscategory}",
        UiText::TagsBuiltinThiscategoryDescription,
        [](const Impl& module, const EvaluationContext& context) {
            return module.ResolveBuiltinThiscategoryTag(context);
        });

    tagRegistry_.RegisterSimple(
        "bindstopall",
        "{bindstopall}",
        "{bindstopall}",
        UiText::TagsBuiltinBindStopAllDescription,
        [](const Impl& module, const EvaluationContext& context) {
            return module.ResolveBuiltinBindStopAllTag(context);
        });

    tagRegistry_.RegisterSimple(
        "targetid",
        "{targetid}",
        "{targetid}",
        UiText::TagsBuiltinTargetIdDescription,
        [](const Impl& module, const EvaluationContext& context) {
            return module.ResolveBuiltinTargetIdTag(context);
        });

    tagRegistry_.RegisterSimple(
        "targetnick",
        "{targetnick}",
        "{targetnick}",
        UiText::TagsBuiltinTargetNickDescription,
        [](const Impl& module, const EvaluationContext& context) {
            return module.ResolveBuiltinTargetNickTag(context);
        });

    tagRegistry_.RegisterSimple(
        "targetrpnick",
        "{targetrpnick}",
        "{targetrpnick}",
        UiText::TagsBuiltinTargetRpNickDescription,
        [](const Impl& module, const EvaluationContext& context) {
            return module.ResolveBuiltinTargetRpNickTag(context);
        });

    tagRegistry_.RegisterSimple(
        "targetname",
        "{targetname}",
        "{targetname}",
        UiText::TagsBuiltinTargetNameDescription,
        [](const Impl& module, const EvaluationContext& context) {
            return module.ResolveBuiltinTargetNameTag(context);
        });

    tagRegistry_.RegisterSimple(
        "targetsurname",
        "{targetsurname}",
        "{targetsurname}",
        UiText::TagsBuiltinTargetSurnameDescription,
        [](const Impl& module, const EvaluationContext& context) {
            return module.ResolveBuiltinTargetSurnameTag(context);
        });

    tagRegistry_.RegisterSimple(
        "targethealth",
        "{targethealth}",
        "{targethealth}",
        UiText::TagsBuiltinTargetHealthDescription,
        [](const Impl& module, const EvaluationContext& context) {
            return module.ResolveBuiltinTargetHealthTag(context);
        });

    tagRegistry_.RegisterSimple(
        "targetarmour",
        "{targetarmour}",
        "{targetarmour}",
        UiText::TagsBuiltinTargetArmourDescription,
        [](const Impl& module, const EvaluationContext& context) {
            return module.ResolveBuiltinTargetArmourTag(context);
        });

    tagRegistry_.RegisterSimple(
        "closestid",
        "{closestid}",
        "{closestid}",
        UiText::TagsBuiltinClosestIdDescription,
        [](const Impl& module, const EvaluationContext& context) {
            return module.ResolveBuiltinClosestIdTag(context);
        });

    tagRegistry_.RegisterSimple(
        "closestidtocenter",
        "{closestidtocenter}",
        "{closestidtocenter}",
        UiText::TagsBuiltinClosestIdToCenterDescription,
        [](const Impl& module, const EvaluationContext& context) {
            return module.ResolveBuiltinClosestIdToCenterTag(context);
        });

    tagRegistry_.RegisterSimple(
        "closestname",
        "{closestname}",
        "{closestname}",
        UiText::TagsBuiltinClosestNameDescription,
        [](const Impl& module, const EvaluationContext& context) {
            return module.ResolveBuiltinClosestNameTag(context);
        });

    tagRegistry_.RegisterSimple(
        "closestsurname",
        "{closestsurname}",
        "{closestsurname}",
        UiText::TagsBuiltinClosestSurnameDescription,
        [](const Impl& module, const EvaluationContext& context) {
            return module.ResolveBuiltinClosestSurnameTag(context);
        });

    tagRegistry_.RegisterSimple(
        "armour",
        "{armour}",
        "{armour}",
        UiText::TagsBuiltinArmourDescription,
        [](const Impl& module, const EvaluationContext& context) {
            return module.ResolveBuiltinArmourTag(context);
        });

    tagRegistry_.RegisterSimple(
        "health",
        "{health}",
        "{health}",
        UiText::TagsBuiltinHealthDescription,
        [](const Impl& module, const EvaluationContext& context) {
            return module.ResolveBuiltinHealthTag(context);
        });

    tagRegistry_.RegisterSimple(
        "ping",
        "{ping}",
        "{ping}",
        UiText::TagsBuiltinPingDescription,
        [](const Impl& module, const EvaluationContext& context) {
            return module.ResolveBuiltinPingTag(context);
        });

    tagRegistry_.RegisterSimple(
        "myx",
        "{myX}",
        "{myX}",
        UiText::TagsBuiltinMyXDescription,
        [](const Impl& module, const EvaluationContext& context) {
            return module.ResolveBuiltinMyXTag(context);
        });

    tagRegistry_.RegisterSimple(
        "myy",
        "{myY}",
        "{myY}",
        UiText::TagsBuiltinMyYDescription,
        [](const Impl& module, const EvaluationContext& context) {
            return module.ResolveBuiltinMyYTag(context);
        });

    tagRegistry_.RegisterSimple(
        "myz",
        "{myZ}",
        "{myZ}",
        UiText::TagsBuiltinMyZDescription,
        [](const Impl& module, const EvaluationContext& context) {
            return module.ResolveBuiltinMyZTag(context);
        });

    tagRegistry_.RegisterSimple(
        "mypos",
        "{mypos}",
        "{mypos}",
        UiText::TagsBuiltinMyPosDescription,
        [](const Impl& module, const EvaluationContext& context) {
            return module.ResolveBuiltinMyPosTag(context);
        });

    tagRegistry_.RegisterSimple(
        "myd",
        "{myd}",
        "{myd}",
        UiText::TagsBuiltinMyDirectionShortDescription,
        [](const Impl& module, const EvaluationContext& context) {
            return module.ResolveBuiltinMyDirectionShortTag(context);
        });

    tagRegistry_.RegisterSimple(
        "mydirection",
        "{mydirection}",
        "{mydirection}",
        UiText::TagsBuiltinMyDirectionDescription,
        [](const Impl& module, const EvaluationContext& context) {
            return module.ResolveBuiltinMyDirectionTag(context);
        });

    tagRegistry_.RegisterSimple(
        "myden",
        "{myden}",
        "{myden}",
        UiText::TagsBuiltinMyDirectionShortEnDescription,
        [](const Impl& module, const EvaluationContext& context) {
            return module.ResolveBuiltinMyDirectionShortEnTag(context);
        });

    tagRegistry_.RegisterSimple(
        "mydirectionen",
        "{mydirectionen}",
        "{mydirectionen}",
        UiText::TagsBuiltinMyDirectionEnDescription,
        [](const Impl& module, const EvaluationContext& context) {
            return module.ResolveBuiltinMyDirectionEnTag(context);
        });

    tagRegistry_.RegisterSimple(
        "mysquare",
        "{mysquare}",
        "{mysquare}",
        UiText::TagsBuiltinMySquareDescription,
        [](const Impl& module, const EvaluationContext& context) {
            return module.ResolveBuiltinMySquareTag(context);
        });

    tagRegistry_.RegisterSimple(
        "mysquareen",
        "{mysquareen}",
        "{mysquareen}",
        UiText::TagsBuiltinMySquareEnDescription,
        [](const Impl& module, const EvaluationContext& context) {
            return module.ResolveBuiltinMySquareEnTag(context);
        });

    tagRegistry_.RegisterSimple(
        "city",
        "{city}",
        "{city}",
        UiText::TagsBuiltinCityDescription,
        [](const Impl& module, const EvaluationContext& context) {
            return module.ResolveBuiltinCityTag(context);
        });

    tagRegistry_.RegisterSimple(
        "cityen",
        "{cityen}",
        "{cityen}",
        UiText::TagsBuiltinCityEnDescription,
        [](const Impl& module, const EvaluationContext& context) {
            return module.ResolveBuiltinCityEnTag(context);
        });

    tagRegistry_.RegisterSimple(
        "clipboard",
        "{clipboard}",
        "{clipboard}",
        UiText::TagsBuiltinClipboardDescription,
        [](const Impl& module, const EvaluationContext& context) {
            return module.ResolveBuiltinClipboardTag(context);
        });

    tagRegistry_.RegisterSimple(
        "mycolor",
        "{mycolor}",
        "{mycolor}",
        UiText::TagsBuiltinMyColorDescription,
        [](const Impl& module, const EvaluationContext& context) {
            return module.ResolveBuiltinMyColorTag(context);
        });

    tagRegistry_.RegisterSimple(
        "mycarhealth",
        "{mycarhealth}",
        "{mycarhealth}",
        UiText::TagsBuiltinMyCarHealthDescription,
        [](const Impl& module, const EvaluationContext& context) {
            return module.ResolveBuiltinMyCarHealthTag(context);
        });

    tagRegistry_.RegisterSimple(
        "mycarspeed",
        "{mycarspeed}",
        "{mycarspeed}",
        UiText::TagsBuiltinMyCarSpeedDescription,
        [](const Impl& module, const EvaluationContext& context) {
            return module.ResolveBuiltinMyCarSpeedTag(context);
        });

    const auto registerMyCarOccupantTag = [this](
                                             std::string name,
                                             UiText description,
                                             MyCarOccupantScope scope,
                                             MyCarOccupantField field) {
        const std::string token = "{" + name + "}";
        tagRegistry_.RegisterSimple(
            std::move(name),
            token,
            token,
            description,
            [scope, field](const Impl& module, const EvaluationContext& context) {
                return module.ResolveBuiltinMyCarOccupantsTag(scope, field, context);
            });
    };

    struct MyCarOccupantTagDef {
        const char* name = nullptr;
        UiText description = UiText::Count;
        MyCarOccupantScope scope = MyCarOccupantScope::Players;
        MyCarOccupantField field = MyCarOccupantField::Id;
    };

    const MyCarOccupantTagDef myCarOccupantTags[] = {
        {"mycarplayersid", UiText::TagsBuiltinMyCarPlayersIdDescription, MyCarOccupantScope::Players, MyCarOccupantField::Id},
        {"mycarplayersname", UiText::TagsBuiltinMyCarPlayersNameDescription, MyCarOccupantScope::Players, MyCarOccupantField::Name},
        {"mycarplayerssurname", UiText::TagsBuiltinMyCarPlayersSurnameDescription, MyCarOccupantScope::Players, MyCarOccupantField::Surname},
        {"mycarplayersnick", UiText::TagsBuiltinMyCarPlayersNickDescription, MyCarOccupantScope::Players, MyCarOccupantField::Nick},
        {"mycarplayersrpnick", UiText::TagsBuiltinMyCarPlayersRpNickDescription, MyCarOccupantScope::Players, MyCarOccupantField::RpNick},
        {"mycarpassengersid", UiText::TagsBuiltinMyCarPassengersIdDescription, MyCarOccupantScope::Passengers, MyCarOccupantField::Id},
        {"mycarpassengersname", UiText::TagsBuiltinMyCarPassengersNameDescription, MyCarOccupantScope::Passengers, MyCarOccupantField::Name},
        {"mycarpassengerssurname", UiText::TagsBuiltinMyCarPassengersSurnameDescription, MyCarOccupantScope::Passengers, MyCarOccupantField::Surname},
        {"mycarpassengersnick", UiText::TagsBuiltinMyCarPassengersNickDescription, MyCarOccupantScope::Passengers, MyCarOccupantField::Nick},
        {"mycarpassengersrpnick", UiText::TagsBuiltinMyCarPassengersRpNickDescription, MyCarOccupantScope::Passengers, MyCarOccupantField::RpNick},
        {"mycarallplayersid", UiText::TagsBuiltinMyCarAllPlayersIdDescription, MyCarOccupantScope::AllPlayers, MyCarOccupantField::Id},
        {"mycarallplayersname", UiText::TagsBuiltinMyCarAllPlayersNameDescription, MyCarOccupantScope::AllPlayers, MyCarOccupantField::Name},
        {"mycarallplayerssurname", UiText::TagsBuiltinMyCarAllPlayersSurnameDescription, MyCarOccupantScope::AllPlayers, MyCarOccupantField::Surname},
        {"mycarallplayersnick", UiText::TagsBuiltinMyCarAllPlayersNickDescription, MyCarOccupantScope::AllPlayers, MyCarOccupantField::Nick},
        {"mycarallplayersrpnick", UiText::TagsBuiltinMyCarAllPlayersRpNickDescription, MyCarOccupantScope::AllPlayers, MyCarOccupantField::RpNick},
        {"mycarallpassengersid", UiText::TagsBuiltinMyCarAllPassengersIdDescription, MyCarOccupantScope::AllPassengers, MyCarOccupantField::Id},
        {"mycarallpassengersname", UiText::TagsBuiltinMyCarAllPassengersNameDescription, MyCarOccupantScope::AllPassengers, MyCarOccupantField::Name},
        {"mycarallpassengerssurname", UiText::TagsBuiltinMyCarAllPassengersSurnameDescription, MyCarOccupantScope::AllPassengers, MyCarOccupantField::Surname},
        {"mycarallpassengersnick", UiText::TagsBuiltinMyCarAllPassengersNickDescription, MyCarOccupantScope::AllPassengers, MyCarOccupantField::Nick},
        {"mycarallpassengersrpnick", UiText::TagsBuiltinMyCarAllPassengersRpNickDescription, MyCarOccupantScope::AllPassengers, MyCarOccupantField::RpNick},
    };

    for (const MyCarOccupantTagDef& tag : myCarOccupantTags) {
        registerMyCarOccupantTag(tag.name, tag.description, tag.scope, tag.field);
    }

    tagRegistry_.RegisterSimple(
        "date",
        "{date}",
        "{date}",
        UiText::TagsBuiltinDateDescription,
        [](const Impl& module, const EvaluationContext& context) {
            return module.ResolveBuiltinDateTag(context);
        });

    tagRegistry_.RegisterSimple(
        "myskin",
        "{myskin}",
        "{myskin}",
        UiText::TagsBuiltinMySkinDescription,
        [](const Impl& module, const EvaluationContext& context) {
            return module.ResolveBuiltinMySkinTag(context);
        });

    tagRegistry_.RegisterSimple(
        "myweapon",
        "{myweapon}",
        "{myweapon}",
        UiText::TagsBuiltinMyWeaponDescription,
        [](const Impl& module, const EvaluationContext& context) {
            return module.ResolveBuiltinMyWeaponTag(context);
        });

    tagRegistry_.RegisterSimple(
        "myweaponid",
        "{myweaponid}",
        "{myweaponid}",
        UiText::TagsBuiltinMyWeaponIdDescription,
        [](const Impl& module, const EvaluationContext& context) {
            return module.ResolveBuiltinMyWeaponIdTag(context);
        });

    tagRegistry_.RegisterSimple(
        "myweaponclip",
        "{myweaponclip}",
        "{myweaponclip}",
        UiText::TagsBuiltinMyWeaponClipDescription,
        [](const Impl& module, const EvaluationContext& context) {
            return module.ResolveBuiltinMyWeaponClipTag(context);
        });

    tagRegistry_.RegisterSimple(
        "mymoney",
        "{mymoney}",
        "{mymoney}",
        UiText::TagsBuiltinMyMoneyDescription,
        [](const Impl& module, const EvaluationContext& context) {
            return module.ResolveBuiltinMyMoneyTag(context);
        });

    tagRegistry_.RegisterSimple(
        "fps",
        "{fps}",
        "{fps}",
        UiText::TagsBuiltinFpsDescription,
        [](const Impl& module, const EvaluationContext& context) {
            return module.ResolveBuiltinFpsTag(context);
        });

    tagRegistry_.RegisterSimple(
        "getvehtype",
        "{getvehtype}",
        "{getvehtype}",
        UiText::TagsBuiltinGetVehTypeDescription,
        [](const Impl& module, const EvaluationContext& context) {
            return module.ResolveBuiltinGetVehTypeTag(context);
        });

    tagRegistry_.RegisterSimple(
        "screen",
        "{screen}",
        "{screen}",
        UiText::TagsBuiltinScreenDescription,
        [](const Impl& module, const EvaluationContext& context) {
            return module.ResolveBuiltinScreenTag(context);
        });

    tagRegistry_.RegisterSimple(
        "tphoto",
        "{tphoto}",
        "{tphoto}",
        UiText::TagsBuiltinTPhotoDescription,
        [](const Impl& module, const EvaluationContext& context) {
            return module.ResolveBuiltinTPhotoTag(context);
        });

    tagRegistry_.RegisterSimple(
        "chatclear",
        "{chatclear}",
        "{chatclear}",
        UiText::TagsBuiltinChatClearDescription,
        [](const Impl& module, const EvaluationContext& context) {
            return module.ResolveBuiltinChatClearTag(context);
        });

    tagRegistry_.RegisterSimple(
        "cursor",
        "{cursor}",
        "abc{cursor}def",
        UiText::TagsBuiltinCursorDescription,
        [](const Impl& module, const EvaluationContext& context) {
            return module.ResolveBuiltinCursorMarkerTag(CursorTarget::SampChat, context);
        });

    tagRegistry_.RegisterSimple(
        "arzcursor",
        "{ARZcursor}",
        "abc{ARZcursor}def",
        UiText::TagsBuiltinArzCursorDescription,
        [](const Impl& module, const EvaluationContext& context) {
            return module.ResolveBuiltinCursorMarkerTag(CursorTarget::ArizonaChat, context);
        });

    tagRegistry_.RegisterSimple(
        "cursordialog",
        "{cursordialog}",
        "abc{cursordialog}def",
        UiText::TagsBuiltinCursorDialogDescription,
        [](const Impl& module, const EvaluationContext& context) {
            return module.ResolveBuiltinCursorMarkerTag(CursorTarget::SampDialog, context);
        });

    tagRegistry_.RegisterSimple(
        "arzcursordialog",
        "{ARZcursordialog}",
        "abc{ARZcursordialog}def",
        UiText::TagsBuiltinArzCursorDialogDescription,
        [](const Impl& module, const EvaluationContext& context) {
            return module.ResolveBuiltinCursorMarkerTag(CursorTarget::ArizonaDialog, context);
        });

    tagRegistry_.RegisterSimple(
        "nickrp",
        "{nickrp}",
        "{nickrp}",
        UiText::TagsBuiltinNickRpDescription,
        [](const Impl& module, const EvaluationContext& context) {
            return module.ResolveBuiltinNickRpTag(context);
        });

    tagRegistry_.RegisterSimple(
        "name",
        "{name}",
        "{name}",
        UiText::TagsBuiltinNameDescription,
        [](const Impl& module, const EvaluationContext& context) {
            return module.ResolveBuiltinNameTag(context);
        });

    tagRegistry_.RegisterSimple(
        "surname",
        "{surname}",
        "{surname}",
        UiText::TagsBuiltinSurnameDescription,
        [](const Impl& module, const EvaluationContext& context) {
            return module.ResolveBuiltinSurnameTag(context);
        });

    tagRegistry_.RegisterSimple(
        "time",
        "{time}",
        "{time}",
        UiText::TagsBuiltinTimeDescription,
        [](const Impl& module, const EvaluationContext& context) {
            return module.ResolveBuiltinTimeTag(context);
        });

    tagRegistry_.RegisterSimple(
        "timenosec",
        "{timenosec}",
        "{timenosec}",
        UiText::TagsBuiltinTimeNoSecDescription,
        [](const Impl& module, const EvaluationContext& context) {
            return module.ResolveBuiltinTimeNoSecTag(context);
        });

    tagRegistry_.RegisterSimple(
        "dialogactive",
        "{dialogactive}",
        "{dialogactive}",
        UiText::TagsBuiltinDialogActiveDescription,
        [](const Impl& module, const EvaluationContext& context) {
            return module.ResolveBuiltinDialogActiveTag(context);
        });

    tagRegistry_.RegisterSimple(
        "dialogcaption",
        "{dialogcaption}",
        "{dialogcaption}",
        UiText::TagsBuiltinDialogCaptionDescription,
        [](const Impl& module, const EvaluationContext& context) {
            return module.ResolveBuiltinDialogCaptionTag(context);
        });

    tagRegistry_.RegisterSimple(
        "dialoggetselecteditem",
        "{dialoggetselecteditem}",
        "{dialoggetselecteditem}",
        UiText::TagsBuiltinDialogGetSelectedItemDescription,
        [](const Impl& module, const EvaluationContext& context) {
            return module.ResolveBuiltinDialogGetSelectedItemTag(context);
        });

    tagRegistry_.RegisterSimple(
        "dialogeditboxtext",
        "{dialogeditboxtext}",
        "{dialogeditboxtext}",
        UiText::TagsBuiltinDialogEditboxTextDescription,
        [](const Impl& module, const EvaluationContext& context) {
            return module.ResolveBuiltinDialogEditboxTextTag(context);
        });

    tagRegistry_.RegisterSimple(
        "dialogselectedindex",
        "{dialogselectedindex}",
        "{dialogselectedindex}",
        UiText::TagsBuiltinDialogSelectedIndexDescription,
        [](const Impl& module, const EvaluationContext& context) {
            return module.ResolveBuiltinDialogSelectedIndexTag(context);
        });

    tagRegistry_.RegisterSimple(
        "dialogwaitopen",
        "{dialogwaitopen}",
        "{dialogwaitopen}",
        UiText::TagsBuiltinDialogWaitOpenDescription,
        [](const Impl& module, const EvaluationContext& context) {
            return module.ResolveBuiltinDialogWaitOpenTag(context);
        });

    tagRegistry_.RegisterSimple(
        "dialogwaitclose",
        "{dialogwaitclose}",
        "{dialogwaitclose}",
        UiText::TagsBuiltinDialogWaitCloseDescription,
        [](const Impl& module, const EvaluationContext& context) {
            return module.ResolveBuiltinDialogWaitCloseTag(context);
        });

    tagRegistry_.RegisterSimple(
        "dialoggetid",
        "{dialoggetid}",
        "{dialoggetid}",
        UiText::TagsBuiltinDialogGetIdDescription,
        [](const Impl& module, const EvaluationContext& context) {
            return module.ResolveBuiltinDialogGetIdTag(context);
        });

    tagRegistry_.RegisterSimple(
        "arzdialoggetinputtext",
        "{ARZdialoggetinputtext}",
        "{ARZdialoggetinputtext}",
        UiText::TagsBuiltinArzDialogGetInputTextDescription,
        [](const Impl& module, const EvaluationContext& context) {
            return module.ResolveBuiltinArzDialogGetInputTextTag(context);
        });

    tagRegistry_.RegisterSimple(
        "arzdialoggetlistitem",
        "{ARZdialoggetlistitem}",
        "{ARZdialoggetlistitem}",
        UiText::TagsBuiltinArzDialogGetListItemDescription,
        [](const Impl& module, const EvaluationContext& context) {
            return module.ResolveBuiltinArzDialogGetListItemTag(context);
        });

    tagRegistry_.RegisterSimple(
        "arzdialogisdialogactive",
        "{ARZdialogisdialogactive}",
        "{ARZdialogisdialogactive}",
        UiText::TagsBuiltinArzDialogIsActiveDescription,
        [](const Impl& module, const EvaluationContext& context) {
            return module.ResolveBuiltinArzDialogIsDialogActiveTag(context);
        });

    tagRegistry_.RegisterSimple(
        "arzdialoggetid",
        "{ARZdialoggetid}",
        "{ARZdialoggetid}",
        UiText::TagsBuiltinArzDialogGetIdDescription,
        [](const Impl& module, const EvaluationContext& context) {
            return module.ResolveBuiltinArzDialogGetIdTag(context);
        });

    tagRegistry_.RegisterSimple(
        "arzdialoggetstyle",
        "{ARZdialoggetstyle}",
        "{ARZdialoggetstyle}",
        UiText::TagsBuiltinArzDialogGetStyleDescription,
        [](const Impl& module, const EvaluationContext& context) {
            return module.ResolveBuiltinArzDialogGetStyleTag(context);
        });

    tagRegistry_.RegisterSimple(
        "arzdialoggettitle",
        "{ARZdialoggettitle}",
        "{ARZdialoggettitle}",
        UiText::TagsBuiltinArzDialogGetTitleDescription,
        [](const Impl& module, const EvaluationContext& context) {
            return module.ResolveBuiltinArzDialogGetTitleTag(context);
        });

    tagRegistry_.RegisterSimple(
        "arzdialoggetbutton1",
        "{ARZdialoggetbutton1}",
        "{ARZdialoggetbutton1}",
        UiText::TagsBuiltinArzDialogGetButton1Description,
        [](const Impl& module, const EvaluationContext& context) {
            return module.ResolveBuiltinArzDialogGetButton1Tag(context);
        });

    tagRegistry_.RegisterSimple(
        "arzdialoggetbutton2",
        "{ARZdialoggetbutton2}",
        "{ARZdialoggetbutton2}",
        UiText::TagsBuiltinArzDialogGetButton2Description,
        [](const Impl& module, const EvaluationContext& context) {
            return module.ResolveBuiltinArzDialogGetButton2Tag(context);
        });

    tagRegistry_.RegisterSimple(
        "arzdialoggetdialogtext",
        "{ARZdialoggetdialogtext}",
        "{ARZdialoggetdialogtext}",
        UiText::TagsBuiltinArzDialogGetDialogTextDescription,
        [](const Impl& module, const EvaluationContext& context) {
            return module.ResolveBuiltinArzDialogGetDialogTextTag(context);
        });

    tagRegistry_.RegisterSimple(
        "arzdialoggetrespond",
        "{ARZdialoggetrespond}",
        "{ARZdialoggetrespond}",
        UiText::TagsBuiltinArzDialogGetRespondDescription,
        [](const Impl& module, const EvaluationContext& context) {
            return module.ResolveBuiltinArzDialogGetRespondTag(context);
        });

    tagRegistry_.RegisterSimple(
        "arzdialogrespondid",
        "{ARZdialogrespondid}",
        "{ARZdialogrespondid}",
        UiText::TagsBuiltinArzDialogRespondIdDescription,
        [](const Impl& module, const EvaluationContext& context) {
            return module.ResolveBuiltinArzDialogRespondIdTag(context);
        });

    tagRegistry_.RegisterSimple(
        "arzdialogrespondbutton",
        "{ARZdialogrespondbutton}",
        "{ARZdialogrespondbutton}",
        UiText::TagsBuiltinArzDialogRespondButtonDescription,
        [](const Impl& module, const EvaluationContext& context) {
            return module.ResolveBuiltinArzDialogRespondButtonTag(context);
        });

    tagRegistry_.RegisterSimple(
        "arzdialogrespondlist",
        "{ARZdialogrespondlist}",
        "{ARZdialogrespondlist}",
        UiText::TagsBuiltinArzDialogRespondListDescription,
        [](const Impl& module, const EvaluationContext& context) {
            return module.ResolveBuiltinArzDialogRespondListTag(context);
        });

    tagRegistry_.RegisterSimple(
        "arzdialogrespondinput",
        "{ARZdialogrespondinput}",
        "{ARZdialogrespondinput}",
        UiText::TagsBuiltinArzDialogRespondInputDescription,
        [](const Impl& module, const EvaluationContext& context) {
            return module.ResolveBuiltinArzDialogRespondInputTag(context);
        });

    tagRegistry_.RegisterFunction(
        "nick",
        "[nick(...)]",
        "[nick(15)]",
        UiText::TagsBuiltinNickFunctionDescription,
        [](const Impl& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBuiltinNickFunctionTag(param, context);
        });

    tagRegistry_.RegisterFunction(
        "rpnick",
        "[rpnick(...)]",
        "[rpnick(15)]",
        UiText::TagsBuiltinRpNickFunctionDescription,
        [](const Impl& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBuiltinRpNickFunctionTag(param, context);
        });

    tagRegistry_.RegisterFunction(
        "name",
        "[name(...)]",
        "[name(15)]",
        UiText::TagsBuiltinNameFunctionDescription,
        [](const Impl& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBuiltinNameFunctionTag(param, context);
        });

    tagRegistry_.RegisterFunction(
        "surname",
        "[surname(...)]",
        "[surname(15)]",
        UiText::TagsBuiltinSurnameFunctionDescription,
        [](const Impl& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBuiltinSurnameFunctionTag(param, context);
        });

    tagRegistry_.RegisterFunction(
        "paramcmd",
        "[paramcmd(...)]",
        "[paramcmd(1+)]",
        UiText::TagsBuiltinParamcmdDescription,
        [](const Impl& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBuiltinParamcmdFunctionTag(param, context);
        });

    tagRegistry_.RegisterFunction(
        "keyemulate",
        "[keyemulate(...)]",
        "[keyemulate(87)]",
        UiText::TagsBuiltinKeyEmulateDescription,
        [](const Impl& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBuiltinKeyEmulateFunctionTag(param, context);
        });

    tagRegistry_.RegisterFunction(
        "math",
        "[math(...)]",
        "[math(2+2)]",
        UiText::TagsBuiltinMathDescription,
        [](const Impl& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBuiltinMathFunctionTag(param, context);
        });

    tagRegistry_.RegisterFunction(
        "numberwithdots",
        "[numberwithdots(...)]",
        "[numberwithdots([math(100*10)])]",
        UiText::TagsBuiltinNumberWithDotsDescription,
        [](const Impl& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBuiltinNumberWithDotsFunctionTag(param, context);
        });

    tagRegistry_.RegisterFunction(
        "armour",
        "[armour(...)]",
        "[armour(15)]",
        UiText::TagsBuiltinArmourFunctionDescription,
        [](const Impl& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBuiltinArmourFunctionTag(param, context);
        });

    tagRegistry_.RegisterFunction(
        "health",
        "[health(...)]",
        "[health(15)]",
        UiText::TagsBuiltinHealthFunctionDescription,
        [](const Impl& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBuiltinHealthFunctionTag(param, context);
        });

    tagRegistry_.RegisterFunction(
        "ping",
        "[ping(...)]",
        "[ping(15)]",
        UiText::TagsBuiltinPingFunctionDescription,
        [](const Impl& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBuiltinPingFunctionTag(param, context);
        });

    tagRegistry_.RegisterFunction(
        "skin",
        "[skin(...)]",
        "[skin(15)]",
        UiText::TagsBuiltinSkinFunctionDescription,
        [](const Impl& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBuiltinSkinFunctionTag(param, context);
        });

    tagRegistry_.RegisterFunction(
        "nickcolor",
        "[nickcolor(...)]",
        "[nickcolor(15)]",
        UiText::TagsBuiltinNickColorFunctionDescription,
        [](const Impl& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBuiltinNickColorFunctionTag(param, context);
        });

    tagRegistry_.RegisterFunction(
        "car",
        "[car(...)]",
        "[car(15)]",
        UiText::TagsBuiltinCarFunctionDescription,
        [](const Impl& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBuiltinCarFunctionTag(param, context);
        });

    tagRegistry_.RegisterFunction(
        "carhealth",
        "[carhealth(...)]",
        "[carhealth(15)]",
        UiText::TagsBuiltinCarHealthFunctionDescription,
        [](const Impl& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBuiltinCarHealthFunctionTag(param, context);
        });

    tagRegistry_.RegisterFunction(
        "keydown",
        "[keydown(...)]",
        "[keydown(87;1000)]",
        UiText::TagsBuiltinKeyDownDescription,
        [](const Impl& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBuiltinKeyDownFunctionTag(param, context);
        });

    tagRegistry_.RegisterFunction(
        "strlow",
        "[strlow(...)]",
        "[strlow(TeSt)]",
        UiText::TagsBuiltinStrLowDescription,
        [](const Impl& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBuiltinStrLowFunctionTag(param, context);
        });

    tagRegistry_.RegisterFunction(
        "addtime",
        "[addtime(...)]",
        "[addtime(10:10:10)]",
        UiText::TagsBuiltinAddTimeDescription,
        [](const Impl& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBuiltinAddTimeFunctionTag(param, context);
        });

    tagRegistry_.RegisterFunction(
        "random",
        "[random(...)]",
        "[random(20-30)]",
        UiText::TagsBuiltinRandomDescription,
        [](const Impl& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBuiltinRandomFunctionTag(param, context);
        });

    tagRegistry_.RegisterFunction(
        "ifandor",
        "[ifandor(...)]",
        "[ifandor({id}==74?[bindstart(30)]:[bindstart(\"fb\" \"\")])]",
        UiText::TagsBuiltinIfAndOrDescription,
        [](const Impl& module, std::string_view param, const EvaluationContext& context, int depth) {
            return module.ResolveBuiltinIfAndOrFunctionTag(param, context, depth);
        });

    tagRegistry_.RegisterFunction(
        "timef",
        "[timef(...)]",
        "[timef(%c;)]",
        UiText::TagsBuiltinTimefDescription,
        [](const Impl& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBuiltinTimefFunctionTag(param, context);
        });

    tagRegistry_.RegisterFunction(
        "getvehtype",
        "[getvehtype(...)]",
        "[getvehtype(15)]",
        UiText::TagsBuiltinGetVehTypeFunctionDescription,
        [](const Impl& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBuiltinGetVehTypeFunctionTag(param, context);
        });

    tagRegistry_.RegisterFunction(
        "screen",
        "[screen(...)]",
        "[screen(Пример)]",
        UiText::TagsBuiltinScreenFunctionDescription,
        [](const Impl& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBuiltinScreenFunctionTag(param, context);
        });

    tagRegistry_.RegisterFunction(
        "wait",
        "[wait(...)]",
        "[wait(1000)]",
        UiText::TagsBuiltinWaitDescription,
        [](const Impl& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBuiltinWaitFunctionTag(param, context);
        });

    tagRegistry_.RegisterFunction(
        "cursor",
        "[cursor(...)]",
        "[cursor(3)]",
        UiText::TagsBuiltinCursorFunctionDescription,
        [](const Impl& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBuiltinCursorFunctionTag(CursorTarget::SampChat, param, context);
        });

    tagRegistry_.RegisterFunction(
        "arzcursor",
        "[ARZcursor(...)]",
        "[ARZcursor(3)]",
        UiText::TagsBuiltinArzCursorFunctionDescription,
        [](const Impl& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBuiltinCursorFunctionTag(CursorTarget::ArizonaChat, param, context);
        });

    tagRegistry_.RegisterFunction(
        "cursordialog",
        "[cursordialog(...)]",
        "[cursordialog(3)]",
        UiText::TagsBuiltinCursorDialogFunctionDescription,
        [](const Impl& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBuiltinCursorFunctionTag(CursorTarget::SampDialog, param, context);
        });

    tagRegistry_.RegisterFunction(
        "arzcursordialog",
        "[ARZcursordialog(...)]",
        "[ARZcursordialog(3)]",
        UiText::TagsBuiltinArzCursorDialogFunctionDescription,
        [](const Impl& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBuiltinCursorFunctionTag(CursorTarget::ArizonaDialog, param, context);
        });

    tagRegistry_.RegisterFunction(
        "dialogclose",
        "[dialogclose(...)]",
        "[dialogclose(1)]",
        UiText::TagsBuiltinDialogCloseDescription,
        [](const Impl& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBuiltinDialogCloseFunctionTag(param, context);
        });

    tagRegistry_.RegisterFunction(
        "dialogsettext",
        "[dialogsettext(...)]",
        "[dialogsettext(Пример)]",
        UiText::TagsBuiltinDialogSetTextDescription,
        [](const Impl& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBuiltinDialogSetTextFunctionTag(param, context);
        });

    tagRegistry_.RegisterFunction(
        "dialogitem",
        "[dialogitem(...)]",
        "[dialogitem(1)]",
        UiText::TagsBuiltinDialogItemDescription,
        [](const Impl& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBuiltinDialogItemFunctionTag(param, context);
        });

    tagRegistry_.RegisterFunction(
        "dialogselect",
        "[dialogselect(...)]",
        "[dialogselect(1)]",
        UiText::TagsBuiltinDialogSelectDescription,
        [](const Impl& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBuiltinDialogSelectFunctionTag(param, context);
        });

    tagRegistry_.RegisterFunction(
        "dialogwaitid",
        "[dialogwaitid(...)]",
        "[dialogwaitid(722)]",
        UiText::TagsBuiltinDialogWaitIdDescription,
        [](const Impl& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBuiltinDialogWaitIdFunctionTag(param, context);
        });

    tagRegistry_.RegisterFunction(
        "dialogresponse",
        "[dialogresponse(...)]",
        "[dialogresponse(1;1;)]",
        UiText::TagsBuiltinDialogResponseDescription,
        [](const Impl& module, std::string_view param, const EvaluationContext& context, int depth) {
            return module.ResolveBuiltinDialogResponseFunctionTag(param, context, depth);
        });

    tagRegistry_.RegisterFunction(
        "dialogtext",
        "[dialogtext(...)]",
        "[dialogtext(0)]",
        UiText::TagsBuiltinDialogTextDescription,
        [](const Impl& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBuiltinDialogTextFunctionTag(param, context);
        });

    tagRegistry_.RegisterFunction(
        "save_dialog",
        "[save_dialog(...)]",
        "[save_dialog()]",
        UiText::TagsBuiltinSaveDialogDescription,
        [](const Impl& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBuiltinSaveDialogFunctionTag(param, context);
        });

    tagRegistry_.RegisterFunction(
        "arzdialogsetinputtext",
        "[ARZdialogsetinputtext(...)]",
        "[ARZdialogsetinputtext(Привет)]",
        UiText::TagsBuiltinArzDialogSetInputTextDescription,
        [](const Impl& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBuiltinArzDialogSetInputTextFunctionTag(param, context);
        });

    tagRegistry_.RegisterFunction(
        "arzdialoggetinputtext",
        "[ARZdialoggetinputtext(...)]",
        "[ARZdialoggetinputtext(500)]",
        UiText::TagsBuiltinArzDialogGetInputTextQueryDescription,
        [](const Impl& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBuiltinArzDialogGetInputTextFunctionTag(param, context);
        });

    tagRegistry_.RegisterFunction(
        "arzdialogclosewithbutton",
        "[ARZdialogclosewithbutton(...)]",
        "[ARZdialogclosewithbutton(1)]",
        UiText::TagsBuiltinArzDialogCloseWithButtonDescription,
        [](const Impl& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBuiltinArzDialogCloseWithButtonFunctionTag(param, context);
        });

    tagRegistry_.RegisterFunction(
        "arzdialogsetlistitem",
        "[ARZdialogsetlistitem(...)]",
        "[ARZdialogsetlistitem(0)]",
        UiText::TagsBuiltinArzDialogSetListItemDescription,
        [](const Impl& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBuiltinArzDialogSetListItemFunctionTag(param, context);
        });

    tagRegistry_.RegisterFunction(
        "arzdialogitem",
        "[ARZdialogitem(...)]",
        "[ARZdialogitem(1)]",
        UiText::TagsBuiltinArzDialogItemDescription,
        [](const Impl& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBuiltinArzDialogItemFunctionTag(param, context);
        });

    tagRegistry_.RegisterFunction(
        "arzdialoggetlistitem",
        "[ARZdialoggetlistitem(...)]",
        "[ARZdialoggetlistitem(500)]",
        UiText::TagsBuiltinArzDialogGetListItemQueryDescription,
        [](const Impl& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBuiltinArzDialogGetListItemFunctionTag(param, context);
        });

    tagRegistry_.RegisterFunction(
        "arzdialoggetdialogtext",
        "[ARZdialoggetdialogtext(...)]",
        "[ARZdialoggetdialogtext(0)]",
        UiText::TagsBuiltinArzDialogGetDialogTextFunctionDescription,
        [](const Impl& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBuiltinArzDialogGetDialogTextFunctionTag(param, context);
        });

    tagRegistry_.RegisterFunction(
        "arzdialogsendrespond",
        "[ARZdialogsendrespond(...)]",
        "[ARZdialogsendrespond({ARZdialoggetid};1;;Привет)]",
        UiText::TagsBuiltinArzDialogSendRespondDescription,
        [](const Impl& module, std::string_view param, const EvaluationContext& context, int depth) {
            return module.ResolveBuiltinArzDialogSendRespondFunctionTag(param, context, depth);
        });

    tagRegistry_.RegisterFunction(
        "binddisable",
        "[binddisable(...)]",
        "[binddisable(30)]",
        UiText::TagsBuiltinBindDisableDescription,
        [](const Impl& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBinderActionFunctionTag("disable", param, context);
        });

    tagRegistry_.RegisterFunction(
        "bindenable",
        "[bindenable(...)]",
        "[bindenable(\"fb\" \"\" \"Основные\")]",
        UiText::TagsBuiltinBindEnableDescription,
        [](const Impl& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBinderActionFunctionTag("enable", param, context);
        });

    tagRegistry_.RegisterFunction(
        "bindstart",
        "[bindstart(...)]",
        "[bindstart(30)]",
        UiText::TagsBuiltinBindStartDescription,
        [](const Impl& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBinderActionFunctionTag("start", param, context);
        });

    tagRegistry_.RegisterFunction(
        "bindstop",
        "[bindstop(...)]",
        "[bindstop({thisbind})]",
        UiText::TagsBuiltinBindStopDescription,
        [](const Impl& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBinderActionFunctionTag("stop", param, context);
        });

    tagRegistry_.RegisterFunction(
        "bindpause",
        "[bindpause(...)]",
        "[bindpause({thisbind})]",
        UiText::TagsBuiltinBindPauseDescription,
        [](const Impl& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBinderActionFunctionTag("pause", param, context);
        });

    tagRegistry_.RegisterFunction(
        "bindunpause",
        "[bindunpause(...)]",
        "[bindunpause({thisbind})]",
        UiText::TagsBuiltinBindUnpauseDescription,
        [](const Impl& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBinderActionFunctionTag("unpause", param, context);
        });

    tagRegistry_.RegisterFunction(
        "bindfastmenu",
        "[bindfastmenu(...)]",
        "[bindfastmenu(\"fb\" \"\")]",
        UiText::TagsBuiltinBindFastMenuDescription,
        [](const Impl& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBinderActionFunctionTag("fastmenu", param, context);
        });

    tagRegistry_.RegisterFunction(
        "bindunfastmenu",
        "[bindunfastmenu(...)]",
        "[bindunfastmenu(\"fb\" \"\")]",
        UiText::TagsBuiltinBindUnfastMenuDescription,
        [](const Impl& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBinderActionFunctionTag("unfastmenu", param, context);
        });

    tagRegistry_.RegisterFunction(
        "bindrandom",
        "[bindrandom(...)]",
        "[bindrandom(\"Команды/Фрапс\" \"Основные\")]",
        UiText::TagsBuiltinBindRandomDescription,
        [](const Impl& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBinderActionFunctionTag("random", param, context);
        });

    tagRegistry_.RegisterFunction(
        "bindended",
        "[bindended(...)]",
        "[bindended({thisbind})]",
        UiText::TagsBuiltinBindEndedDescription,
        [](const Impl& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBinderActionFunctionTag("ended", param, context);
        });

    tagRegistry_.RegisterFunction(
        "bindpopup",
        "[bindpopup(...)]",
        "[bindpopup(\"№30 fb\")]",
        UiText::TagsBuiltinBindPopupDescription,
        [](const Impl& module, std::string_view param, const EvaluationContext& context, int) {
            return module.ResolveBinderActionFunctionTag("popup", param, context);
        });

    RefreshCatalogEntries();
}
