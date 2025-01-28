// Fill out your copyright notice in the Description page of Project Settings.


#include "Characters/SlashCharacter.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/InputComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedInputComponent.h"
#include "GroomComponent.h"
#include "Components/AttributeComponent.h"
#include "Items/Item.h"
#include "Items/Weapons/Weapon.h"
#include "Animation/AnimMontage.h"
#include "HUD/SlashHUD.h"
#include "HUD/SlashOverlay.h"
#include "Items/Soul.h"
#include "Items/Treasure.h"
#include "Kismet/KismetSystemLibrary.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"
#include "Enemy/Enemy.h"

ASlashCharacter::ASlashCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.f, 400.f, 0.f);

	GetMesh()->SetCollisionObjectType(ECollisionChannel::ECC_WorldDynamic);
	GetMesh()->SetCollisionResponseToAllChannels(ECollisionResponse::ECR_Ignore);
	GetMesh()->SetCollisionResponseToChannel(ECollisionChannel::ECC_Visibility, ECollisionResponse::ECR_Block);
	GetMesh()->SetCollisionResponseToChannel(ECollisionChannel::ECC_WorldDynamic, ECollisionResponse::ECR_Overlap);
	GetMesh()->SetGenerateOverlapEvents(true);

	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
	SpringArm->SetupAttachment(GetRootComponent());
	SpringArm->TargetArmLength = 300.f;
	SpringArm->bUsePawnControlRotation = true;

	ViewCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("ViewCamera"));
	ViewCamera->SetupAttachment(SpringArm);

	AutoPossessPlayer = EAutoReceiveInput::Player0;

	Hair = CreateDefaultSubobject<UGroomComponent>(TEXT("Hair"));
	Hair->SetupAttachment(GetMesh());
	Hair->AttachmentName = FString("head");

	Eyebrows = CreateDefaultSubobject<UGroomComponent>(TEXT("Eyebrows"));
	Eyebrows->SetupAttachment(GetMesh());
	Eyebrows->AttachmentName = FString("head");
}

void ASlashCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (Attributes && SlashOverlay)
	{
		Attributes->RegenStamina(DeltaTime);
		SlashOverlay->SetStaminaBarPercent(Attributes->GetStaminaPercent());
		if (ActionState == EActionState::EAS_Unoccupied)
		{
			Attributes->RegenHealth(DeltaTime);
			SlashOverlay->SetHealthBarPercent(Attributes->GetHealthPercent());
		}
	}

	if (bLockOnEngaged && TargetActor)
	{
		bool bIsAlive = !TargetActor->ActorHasTag("Dead");
		// Check distance to target
		float DistanceToTarget = FVector::Dist(GetActorLocation(), TargetActor->GetActorLocation());
		if (!bIsAlive || DistanceToTarget > 1000.f) // Adjust the distance as needed
		{
			DisengageLock();
		}

		UpdateLockOnRotation();
	}
}

void ASlashCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (UEnhancedInputComponent* EnhancedInputComponent = CastChecked<UEnhancedInputComponent>(PlayerInputComponent))
	{
		EnhancedInputComponent->BindAction(MovementAction, ETriggerEvent::Triggered, this, &ASlashCharacter::Move);
		EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &ASlashCharacter::Look);
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Started, this, &ASlashCharacter::Jump);
		EnhancedInputComponent->BindAction(EKeyAction, ETriggerEvent::Started, this, &ASlashCharacter::EKeyPressed);
		EnhancedInputComponent->BindAction(AttackAction, ETriggerEvent::Started, this, &ASlashCharacter::Attack);
		EnhancedInputComponent->BindAction(StowAction, ETriggerEvent::Started, this, &ASlashCharacter::Stow);
		EnhancedInputComponent->BindAction(DodgeAction, ETriggerEvent::Started, this, &ASlashCharacter::Dodge);
		EnhancedInputComponent->BindAction(LockTargetAction, ETriggerEvent::Started, this, &ASlashCharacter::LockTarget);
	}
}

float ASlashCharacter::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	HandleDamage(DamageAmount);
	SetHUDHealth();
	return DamageAmount;
}

void ASlashCharacter::Jump()
{
	if (IsUnoccupied())
	{
		Super::Jump();
	}
}

void ASlashCharacter::GetHit_Implementation(const FVector& ImpactPoint, AActor* Hitter)
{
	Super::GetHit_Implementation(ImpactPoint, Hitter);

	SetWeaponCollisionEnabled(ECollisionEnabled::NoCollision);
	if (Attributes &&Attributes->GetHealthPercent() > 0.f)
	{
		ActionState = EActionState::EAS_HitReaction;
	}
}

void ASlashCharacter::SetOverlappingItem(AItem* Item)
{
	OverlappingItem = Item;
}

void ASlashCharacter::AddSouls(ASoul* Soul)
{
	if (Attributes && SlashOverlay)
	{
		Attributes->AddSouls(Soul->GetSouls());
		SlashOverlay->SetSouls(Attributes->GetSouls());
	}
}

void ASlashCharacter::AddGold(ATreasure* Treasure)
{
	if (Attributes && SlashOverlay)
	{
		Attributes->AddGold(Treasure->GetGold());
		SlashOverlay->SetGold(Attributes->GetGold());
	}
}

void ASlashCharacter::BeginPlay()
{
	Super::BeginPlay();

	if (APlayerController* PlayerController = Cast<APlayerController>(Controller))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer()))
		{
			Subsystem->AddMappingContext(SlashMappingContext, 0);
		}
	}
	Tags.Add(FName("EngageableTarget"));

	InitializeSlashOverlay();
}

void ASlashCharacter::Move(const FInputActionValue& Value)
{
	if (ActionState != EActionState::EAS_Unoccupied) return;
	const FVector2D MovementVector = Value.Get<FVector2D>();
	if (Controller)
	{
		const FRotator ControlRotation = GetControlRotation();
		const FRotator YawRotation(0.f, ControlRotation.Yaw, 0.f);

		const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
		AddMovementInput(ForwardDirection, MovementVector.Y);
		const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);
		AddMovementInput(RightDirection, MovementVector.X);
	}
}

void ASlashCharacter::Look(const FInputActionValue& Value)
{
	const FVector2D LookAxisValue = Value.Get<FVector2D>();
	if (Controller)
	{
		AddControllerYawInput(LookAxisValue.X);
		AddControllerPitchInput(LookAxisValue.Y);
	}
}

void ASlashCharacter::EKeyPressed()
{
	AWeapon* OverlappingWeapon = Cast<AWeapon>(OverlappingItem);
	if (OverlappingWeapon)
	{
		if (EquippedWeapon)
		{
			EquippedWeapon->Destroy();
		}
		EquipWeapon(OverlappingWeapon);
	}
}

void ASlashCharacter::Attack()
{		
	Super::Attack();

	if (CanAttack())
	{
		PlayAttackMontage();
		ActionState = EActionState::EAS_Attacking;
	}
}

void ASlashCharacter::Stow()
{
	if (CanDisarm())
	{
		Disarm();
	}
	else if (CanArm())
	{
		Arm();
	}
}

void ASlashCharacter::Dodge()
{
	if (IsOccupied() || !HasEnoughStamina()) return;

	PlayDodgeMontage();
	ActionState = EActionState::EAS_Dodge;
	if (Attributes && SlashOverlay)
	{
		Attributes->UseStamina(Attributes->GetDodgeCost());
		SlashOverlay->SetStaminaBarPercent(Attributes->GetStaminaPercent());
	}
}

void ASlashCharacter::LockTarget()
{
	if (!bLockOnEngaged)
	{
		EngageLock();
	}
	else
	{
		DisengageLock();
	}
}

void ASlashCharacter::EquipWeapon(AWeapon* Weapon)
{
	Weapon->Equip(GetMesh(), FName("RightHandSocket"), this, this);
	CharacterState = ECharacterState::ECS_EquippedWeapon;
	EquippedWeapon = Weapon;
}

bool ASlashCharacter::CanAttack()
{
	return ActionState == EActionState::EAS_Unoccupied &&
		CharacterState != ECharacterState::ECS_Unequipped;
}

void ASlashCharacter::AttackEnd()
{
	ActionState = EActionState::EAS_Unoccupied;
}

void ASlashCharacter::DodgeEnd()
{
	Super::DodgeEnd();

	ActionState = EActionState::EAS_Unoccupied;
}

void ASlashCharacter::Die_Implementation()
{
	Super::Die_Implementation();

	ActionState = EActionState::EAS_Dead;
	DisableMeshCollision();
	bLockOnEngaged = false;
}

bool ASlashCharacter::HasEnoughStamina()
{
	return Attributes && Attributes->GetStamina() > Attributes->GetDodgeCost();
}

bool ASlashCharacter::IsOccupied()
{
	return ActionState != EActionState::EAS_Unoccupied;
}

void ASlashCharacter::EngageLock()
{
	if(CanAttack())
	{
		float ClosestDistance = 1000000.f;
		TargetActor = nullptr;
		TArray<FHitResult> HitActors;

		const FVector Start = GetActorLocation();
		const FVector End = GetActorLocation();

		TArray<AActor*> ActorsToIgnore;
		ActorsToIgnore.Add(GetOwner());

		TArray<TEnumAsByte<EObjectTypeQuery>> ObjectTypes;
		ObjectTypes.Add(UEngineTypes::ConvertToObjectType(ECollisionChannel::ECC_Pawn));

		// Perform the sphere trace
		bool bHit = UKismetSystemLibrary::SphereTraceMultiForObjects(
			this, 
			Start, 
			End, 
			750.f, 
			ObjectTypes, 
			false, 
			ActorsToIgnore, 
			bShowSphereDebug ? EDrawDebugTrace::ForDuration : EDrawDebugTrace::None, 
			HitActors, 
			true
		);

		// Process the results
		if(bHit)
		{
			for (const FHitResult& Hit : HitActors)
			{
				AActor* HitActor = Hit.GetActor();
				if (HitActor)
				{
					float DistanceToTarget = FVector::Dist(GetActorLocation(), Hit.ImpactPoint);
					if (DistanceToTarget < ClosestDistance)
					{
						ClosestDistance = DistanceToTarget;
						TargetActor = HitActor;
					}
				}
			}
			if (TargetActor)
			{
				LockOnTarget(TargetActor);
				bLockOnEngaged = true;
			}
			CombatTarget = TargetActor;
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("No Hits Detected"));
		} 
	}
	else
	{
		bLockOnEngaged = false;
	}
}

void ASlashCharacter::LockOnTarget(AActor* InTargetActor)
{
	if (GetWorld())
	{
		APlayerController* PlayerController = GetWorld()->GetFirstPlayerController();
		ACharacter* Character = Cast<ACharacter>(this);

		if (PlayerController && Character && SpringArm)
		{
			// Lock the camera onto the target
			PlayerController->SetIgnoreLookInput(true);

			// Adjust character movement
			SpringArm->bEnableCameraRotationLag = true;
			Character->bUseControllerRotationYaw = true;
			Character->GetCharacterMovement()->bOrientRotationToMovement = false;
			Character->GetCharacterMovement()->bUseControllerDesiredRotation = true;

			// Set timer to update lock-on rotation
			GetWorld()->GetTimerManager().SetTimer(LockOnTimerHandle, this, &ASlashCharacter::UpdateLockOnRotation, 0.1f, true);
		}
	}
}

void ASlashCharacter::UpdateLockOnRotation()
{
	if (bLockOnEngaged && TargetActor)
	{
		APlayerController* PlayerController = GetWorld()->GetFirstPlayerController();

		if (PlayerController)
		{
			FRotator ControlRotation = (TargetActor->GetActorLocation() - GetActorLocation()).Rotation();
			PlayerController->SetControlRotation(ControlRotation);
		}
	}
}

void ASlashCharacter::DisengageLock()
{
	APlayerController* PlayerController = GetWorld()->GetFirstPlayerController();
	ACharacter* Character = Cast<ACharacter>(this);

	if (PlayerController && Character && SpringArm)
	{
		// Unlock the camera
		PlayerController->SetIgnoreLookInput(false);

		// Reset character movement
		SpringArm->bEnableCameraRotationLag = false;
		Character->bUseControllerRotationYaw = false;
		Character->GetCharacterMovement()->bOrientRotationToMovement = true;
		Character->GetCharacterMovement()->bUseControllerDesiredRotation = false;

		// Clear timer
		GetWorld()->GetTimerManager().ClearTimer(LockOnTimerHandle);

		bLockOnEngaged = false;
		TargetActor = nullptr;
		CombatTarget = nullptr;
	}
}

bool ASlashCharacter::CanArm()
{
	return ActionState == EActionState::EAS_Unoccupied &&
		CharacterState == ECharacterState::ECS_Unequipped &&
		EquippedWeapon;
}

bool ASlashCharacter::CanDisarm()
{
	return ActionState == EActionState::EAS_Unoccupied &&
		CharacterState != ECharacterState::ECS_Unequipped;
}

void ASlashCharacter::Arm()
{
	PlayStowMontage(FName("Equip"));
	CharacterState = ECharacterState::ECS_EquippedWeapon;
	ActionState = EActionState::EAS_Stowing;
}

void ASlashCharacter::Disarm()
{
	PlayStowMontage(FName("Unequip"));
	CharacterState = ECharacterState::ECS_Unequipped;
	ActionState = EActionState::EAS_Stowing;
}

void ASlashCharacter::PlayStowMontage(const FName& SectionName)
{
	UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance();
	if (AnimInstance && StowMontage)
	{
		AnimInstance->Montage_Play(StowMontage);
		AnimInstance->Montage_JumpToSection(SectionName, StowMontage);
	}
}

void ASlashCharacter::AttachWeaponToBack()
{
	if (EquippedWeapon)
	{
		EquippedWeapon->AttachMeshToSocket(GetMesh(), FName("SpineSocket"));
	}
}

void ASlashCharacter::AttachWeaponToHand()
{
	if (EquippedWeapon)
	{
		EquippedWeapon->AttachMeshToSocket(GetMesh(), FName("RightHandSocket"));
	}
}

void ASlashCharacter::FinishStowing()
{
	ActionState = EActionState::EAS_Unoccupied;
}

void ASlashCharacter::HitReactEnd()
{
	ActionState = EActionState::EAS_Unoccupied;
}

bool ASlashCharacter::IsUnoccupied()
{
	return ActionState == EActionState::EAS_Unoccupied;
}

void ASlashCharacter::InitializeSlashOverlay()
{
	APlayerController* PlayerController = Cast<APlayerController>(GetController());
	if (PlayerController)
	{
		ASlashHUD* SlashHUD = Cast<ASlashHUD>(PlayerController->GetHUD());
		if (SlashHUD)
		{
			SlashOverlay = SlashHUD->GetSlashOverlay();
			if (SlashOverlay && Attributes)
			{
				SlashOverlay->SetHealthBarPercent(Attributes->GetHealthPercent());
				SlashOverlay->SetStaminaBarPercent(1.f);
				SlashOverlay->SetGold(0);
				SlashOverlay->SetSouls(0);
			}
		}
	}
}

void ASlashCharacter::SetHUDHealth()
{
	if (SlashOverlay && Attributes)
	{
		SlashOverlay->SetHealthBarPercent(Attributes->GetHealthPercent());
	}
}