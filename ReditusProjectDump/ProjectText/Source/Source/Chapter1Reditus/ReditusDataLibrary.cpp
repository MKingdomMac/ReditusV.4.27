// Fill out your copyright notice in the Description page of Project Settings.


#include "ReditusDataLibrary.h"
#include "Engine/DataTable.h"

bool UReditusDataLibrary::GetEnemyDefinition(
	UDataTable* EnemyDataTable,
	FName RowName,
	FReditusEnemyDefinition& OutEnemyDefinition)
{
	if (!EnemyDataTable)
	{
		OutEnemyDefinition = FReditusEnemyDefinition();
		return false;
	}

	static const FString ContextString(TEXT("GetEnemyDefinition"));

	const FReditusEnemyDefinition* EnemyRow =
		EnemyDataTable->FindRow<FReditusEnemyDefinition>(
			RowName,
			ContextString,
			true
		);

	if (!EnemyRow)
	{
		OutEnemyDefinition = FReditusEnemyDefinition();
		return false;
	}

	OutEnemyDefinition = *EnemyRow;
	return true;
}
