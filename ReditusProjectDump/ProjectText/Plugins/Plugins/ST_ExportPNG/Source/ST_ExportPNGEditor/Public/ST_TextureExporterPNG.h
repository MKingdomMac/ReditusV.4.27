// Copyright (C) James Baxter. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Exporters/Exporter.h"
#include "ST_TextureExporterPNG.generated.h"

// Declarations
struct FTextureSource;

/*
* Texture Exporter for PNG Files
*/
UCLASS()
class UST_TextureExporterPNG : public UExporter
{
	GENERATED_BODY()
public:
	UST_TextureExporterPNG(const FObjectInitializer& OI);

	virtual bool SupportsObject(UObject* Object) const override;
	virtual bool ExportBinary(UObject* Object, const TCHAR* Type, FArchive& Ar, FFeedbackContext* Warn, int32 FileIndex = 0, uint32 PortFlags = 0) override;
};