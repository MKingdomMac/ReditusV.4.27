#include "Modules/ModuleManager.h"
#include "DialogueUserWidget.h"
#include "ReditusDialoguePresentationWidget.h"
#include "HAL/IConsoleManager.h"

namespace
{
    TAutoConsoleVariable<int32> DialogueStyle(TEXT("Reditus.DialogueStyle"), 1,
        TEXT("1: style supported dialogue widgets before construction; 0: keep their original presentation for new conversations."));

    void ApplyDialogueStyle(UDialogueUserWidget* Widget)
    {
        if (DialogueStyle.GetValueOnGameThread() != 0) UReditusDialoguePresentationWidget::Install(Widget);
    }
}

class FReditusQuestNPCModule : public IModuleInterface
{
    FDelegateHandle PresentationHandle;
public:
    virtual void StartupModule() override
    {
        PresentationHandle = UDialogueUserWidget::OnInitializedNative().AddStatic(&ApplyDialogueStyle);
    }
    virtual void ShutdownModule() override
    {
        UDialogueUserWidget::OnInitializedNative().Remove(PresentationHandle);
    }
};

IMPLEMENT_MODULE(FReditusQuestNPCModule, ReditusQuestNPC)
