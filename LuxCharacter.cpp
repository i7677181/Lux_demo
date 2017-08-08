// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.
// @file LuxCharacter 
// @brief Main character class, consists of movement and collection methods
///
#include "Lux.h"
#include "Kismet/HeadMountedDisplayFunctionLibrary.h"
#include "LuxCharacter.h"
#include "Pickup.h"
#include "FireflyPickup.h"
#include "NaturePickup.h"


bool visited = false;
float drainedPower;
bool airTime=false;
bool root = false;
float FireflyPower;
float _absorbedPower=0;
bool destroyFF = false;
bool collect=true;
bool absorb = true;
//APickup*  TestPickup;
//AFireflyPickup* TestFirefly;
ALuxCharacter::ALuxCharacter()
{ 

	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.0f);

	// set our turn rates for input
	BaseTurnRate = 45.f;
	BaseLookUpRate = 45.f;

	// Don't rotate when the controller rotates. Let that just affect the camera.
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	// Configure character movement
	GetCharacterMovement()->bOrientRotationToMovement = true; // Character moves in the direction of input...	
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 540.0f, 0.0f); // ...at this rotation rate
	GetCharacterMovement()->JumpZVelocity = 1600.f;
	GetCharacterMovement()->AirControl = 0.2f;
	float RunSpeed = 300;
	GetCharacterMovement()->MaxWalkSpeed += RunSpeed;
	//maxSpeed = GetCharacterMovement()->MaxWalkSpeed;
	//GetCharacterMovement()->MaxWalkSpeed = minSpeed;

	// Create a camera boom (pulls in towards the player if there is a collision)
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 300.0f; // The camera follows at this distance behind the character	
	CameraBoom->bUsePawnControlRotation = true; // Rotate the arm based on the controller

	// Create a follow camera
	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName); // Attach the camera to the end of the boom and let the boom adjust to match the controller orientation
	FollowCamera->bUsePawnControlRotation = false; // Camera does not rotate relative to arm

	FVector offset;
	offset.Z = -5;
	//create collection sphere
	CollectionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("CollectionSphere"));
	//attachTo deprecated
	CollectionSphere->SetupAttachment(RootComponent);
	//CollectionSphere->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepWorldTransform)*offset;/**offset*/;

	//sphere size
	CollectionSphere->SetSphereRadius(100.f);
	
	//set base power
	InitialPower = 0.f;
	CharacterPower = InitialPower;
	absorbedPower = 0;
	isAbsorbing = false;
	isMovingForward = false;
	CollectedPower = 0;

	// Gets the active character movement from the ACharacter class
	UCharacterMovementComponent* characterMovement = ACharacter::GetCharacterMovement();
	// This variable is set in the .h file
	baseGravityVal = 3.0;
	// Set gravity to the base gravity. baseGravityVal can be adjusted to what we want
	characterMovement->GravityScale = baseGravityVal;

	// Note: The skeletal mesh and anim blueprint references on the Mesh component (inherited from Character) 
	// are set in the derived blueprint asset named MyCharacter (to avoid direct content references in C++)

}

//////////////////////////////////////////////////////////////////////////
// Input

void ALuxCharacter::SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent)
{
	// Set up gameplay key bindings
	check(PlayerInputComponent);

	// JUMP ACTION.
	// Instead of running a function in ACharacter it runs it in this class instead.
	PlayerInputComponent->BindAction("Jump", IE_Pressed, this, &ALuxCharacter::Jump);
	PlayerInputComponent->BindAction("Jump", IE_Released, this, &ALuxCharacter::JumpRelease);
	
	//Character interaction/collection
	PlayerInputComponent->BindAction("Collect", IE_Repeat, this, &ALuxCharacter::CollectPower); 
	PlayerInputComponent->BindAction("Collect", IE_Released, this, &ALuxCharacter::StopCollect);

	PlayerInputComponent->BindAxis("MoveForward", this, &ALuxCharacter::MoveForward);
	PlayerInputComponent->BindAxis("MoveRight", this, &ALuxCharacter::MoveRight);

	// We have 2 versions of the rotation bindings to handle different kinds of devices differently
	// "turn" handles devices that provide an absolute delta, such as a mouse.
	// "turnrate" is for devices that we choose to treat as a rate of change, such as an analog joystick
	PlayerInputComponent->BindAxis("Turn", this, &APawn::AddControllerYawInput);
	PlayerInputComponent->BindAxis("TurnRate", this, &ALuxCharacter::TurnAtRate);
	PlayerInputComponent->BindAxis("LookUp", this, &APawn::AddControllerPitchInput);
	PlayerInputComponent->BindAxis("LookUpRate", this, &ALuxCharacter::LookUpAtRate);

	// handle touch devices
	PlayerInputComponent->BindTouch(IE_Pressed, this, &ALuxCharacter::TouchStarted);
	PlayerInputComponent->BindTouch(IE_Released, this, &ALuxCharacter::TouchStopped);

	// VR headset functionality
	PlayerInputComponent->BindAction("ResetVR", IE_Pressed, this, &ALuxCharacter::OnResetVR);
}


void ALuxCharacter::OnResetVR()
{
	UHeadMountedDisplayFunctionLibrary::ResetOrientationAndPosition();
}

void ALuxCharacter::TouchStarted(ETouchIndex::Type FingerIndex, FVector Location)
{
	// jump, but only on the first touch
	if (FingerIndex == ETouchIndex::Touch1)
	{
		Jump();
	}
}

void ALuxCharacter::TouchStopped(ETouchIndex::Type FingerIndex, FVector Location)
{
	if (FingerIndex == ETouchIndex::Touch1)
	{
		StopJumping();
	}
}

void ALuxCharacter::TurnAtRate(float Rate)
{
	// calculate delta for this frame from the rate information
	AddControllerYawInput(Rate * BaseTurnRate * GetWorld()->GetDeltaSeconds());
}

void ALuxCharacter::LookUpAtRate(float Rate)
{
	// calculate delta for this frame from the rate information
	AddControllerPitchInput(Rate * BaseLookUpRate * GetWorld()->GetDeltaSeconds());
}

void ALuxCharacter::MoveForward(float Value)
{
	if (isFirstInteractionDone == false) { return; }

	if ((Controller != NULL) && (Value != 0.0f))
	{	isMovingForward = true;
		// find out which way is forward
		const FRotator Rotation = Controller->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);
		// get forward vector
		const FVector Direction = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
		AddMovementInput(Direction, Value);		
	}
//	else isMovingForward = false, GetCharacterMovement()->MaxWalkSpeed = minSpeed;
}

void ALuxCharacter::MoveRight(float Value)
{
	if (isFirstInteractionDone == false) { return; }

	if ( (Controller != NULL) && (Value != 0.0f) )
	{
		// find out which way is right
		const FRotator Rotation = Controller->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);
	
		// get right vector 
		const FVector Direction = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);
		// add movement in that direction
		AddMovementInput(Direction, Value);
	}
}

// Called every frame
void ALuxCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if(airTime==true) //add timer func
	{
		drainedPower -= DeltaTime;
		UpdatePower(drainedPower);
		UE_LOG(LogTemp, Warning, TEXT("%d"),CharacterPower);

	}

	if (isAbsorbing)
	{
		CollectedPower++;
		if (CollectedPower > 1000) isAbsorbing = false; destroyFF = true; //if over hald=f delete ff
		UpdatePower(CollectedPower);
		GetCurrentPower();
	}
	if (CollectedPower >= 1000) UE_LOG(LogTemp, Warning, TEXT("STOP updating power\n")); isAbsorbing = false; destroyFF = true;
	
}
void ALuxCharacter::CollectPower()
{
	//get all overlapping actors and store in an array
	TArray<AActor*> CollectedActors;
	CollectionSphere->GetOverlappingActors(CollectedActors);
	//for each collected actor 
	for (int32 iCollected = 0; iCollected < CollectedActors.Num(); ++iCollected)
	{
			//cast actor to APickup
			APickup* const TestPickup = Cast<APickup>(CollectedActors[iCollected]);
			//check if pickup is firefly object
			if (TestPickup && !TestPickup->IsPendingKill() && TestPickup->IsActive())
			{
				AFireflyPickup* const TestFirefly = Cast<AFireflyPickup>(TestPickup);
				if (TestFirefly) //test if pickup is firefly
				{
					if (CollectedPower<1000)isAbsorbing = true; //checks current collected power updated in tick()
					//if (destroyFF) TestFirefly->WasCollected(); //stop absorbing delete firefly
				}

			}

	}//forloop end
	
}//end
void ALuxCharacter::StopCollect()
{//reset
	isAbsorbing = false;
	_absorbedPower = 0;
	//if (CollectedPower > 500) TestFirefly->WasCollected();
	CollectedPower = 0;
}
float ALuxCharacter::GetInitialPower()
{
	return InitialPower;
}

float ALuxCharacter::GetCurrentPower()
{
	return CharacterPower;
}
void ALuxCharacter::UpdatePower(float PowerChange)
{
	CharacterPower = CharacterPower + PowerChange;
	if (CharacterPower <= 0) CharacterPower = 0; //cap min
	if (CharacterPower > 5000) CharacterPower = 5000;//PowerChangeFX();  //max

	
}

// JUMP BUTTON PRESSED
// called everytime space is pressed
void ALuxCharacter::Jump()
{

	// If the character has not stood up yet, don't jump.
	if (isFirstInteractionDone == false) { return; }

	// Gets the active character movement from the ACharacter class
	UCharacterMovementComponent* characterMovement = ACharacter::GetCharacterMovement();
	ACharacter* myCharacter = UGameplayStatics::GetPlayerCharacter(GetWorld(), 0);
	// NORMAL JUMP
	float JumpHeight=1600;
	// MUSHROOM DOUBLEJUMP
	TArray<AActor*> CollectedActors;
	CollectionSphere->GetOverlappingActors(CollectedActors);
	
	//keep track of collected pickups
	float CollectedPower = 0;
	//delete
	FVector c;
	c = myCharacter->GetActorLocation();
	UE_LOG(LogTemp, Warning, TEXT("char pos is %s"), *c.ToString());
	//for each collected actor 
	for (int32 iCollected = 0; iCollected < CollectedActors.Num(); ++iCollected)
	{
		//cast actor to APickup
		APickup* const TestPickup = Cast<APickup>(CollectedActors[iCollected]);
		//check if player is near mushroom or nature object
		if (TestPickup && !TestPickup->IsPendingKill() && TestPickup->IsActive())
		{   	
			ANaturePickup* const TestNature = Cast<ANaturePickup>(TestPickup);
			if (TestNature ) {
				UE_LOG(LogTemp, Warning, TEXT("nature \n"));
				if(TestNature->isRoot()==false && CharacterPower<=1000) //if not root and power is under half
				{
					UE_LOG(LogTemp, Warning, TEXT("not root \n"));
					FVector dist;
					FVector m;
					m = TestPickup->GetActorLocation();
					dist.Z = (c.Z - m.Z);
					if (dist.Z > 70 /* && CharacterPower < 1000*/) //if player ontop of nature object and has little Firefly Power he can double jump
					{
						UE_LOG(LogTemp, Warning, TEXT("on mushroom \n"));
						JumpHeight = 3400;
					//	doubleJump = true;
					}
					else if (CharacterPower > 1000) UE_LOG(LogTemp, Warning, TEXT("need more Narure Power \n"));
				}

			}
		}
		
	}

	//normal jump
	if (characterMovement->IsMovingOnGround() && !characterMovement->IsFalling())
	{
		//set velocity in up direction
		characterMovement->Velocity.Z = JumpHeight;

		// Manually set movement mode. This makes the animations play I believe.
		characterMovement->SetMovementMode(MOVE_Falling);

		// Log to make sure everything is running
		UE_LOG(LogTemp, Warning, TEXT("jump"));
		JumpHeight = 1600; //normal jump
	}	

		//  floating
		else if (!isFloating && CharacterPower > 0 && !characterMovement->IsMovingOnGround())  /// can't press space to float again?
		{
			isFloating = true;
			airTime = true;
			characterMovement->Velocity.Z= characterMovement->Velocity.Z / 4;
			characterMovement->GravityScale = 0.2;
		}

}

// JUMP BUTTON RELEASED
void ALuxCharacter::JumpRelease()
{
	ACharacter* myCharacter = UGameplayStatics::GetPlayerCharacter(GetWorld(), 0);
	// Gets the active character movement from the ACharacter class
	UCharacterMovementComponent* characterMovement = ACharacter::GetCharacterMovement();
	characterMovement->SetMovementMode(MOVE_Falling);
	// Check if character is still in the air
	if (!characterMovement->IsMovingOnGround() && characterMovement->IsFalling())
	{
		UE_LOG(LogTemp, Warning, TEXT("falling"));
		characterMovement->GravityScale = baseGravityVal;

		if (isFloating) //LANDING 
		{
			UE_LOG(LogTemp, Warning, TEXT("landing"));
			// Set gravity back to normal
			characterMovement->GravityScale = baseGravityVal; 

			isFloating = false;
			airTime = false;
			//characterMovement->SetMovementMode(MOVE_Falling);
			
		}
	}

}

