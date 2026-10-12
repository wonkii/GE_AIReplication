#include "SharpMovementMetrics.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Guid.h"
#include "HAL/FileManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogSharpMetrics, Log, All);

namespace
{
	struct FTable
	{
		TArray<FString> Header;
		TArray<TArray<FString>> Rows;
		bool Load(const FString& Path, const TArray<FString>& Required, FString& Error)
		{
			FString Text;
			if (!FFileHelper::LoadFileToString(Text, *Path)) { Error = TEXT("Cannot read: ") + Path; return false; }
			TArray<TArray<FString>> Parsed;
			TArray<FString> Row;
			FString Cell;
			bool Quoted = false, Closed = false;
			auto FinishRow = [&]()
			{
				Row.Add(Cell); Cell.Empty(); Closed = false;
				if (!(Row.Num() == 1 && Row[0].IsEmpty())) { Parsed.Add(MoveTemp(Row)); }
				Row.Reset();
			};
			for (int32 I = 0; I < Text.Len(); ++I)
			{
				const TCHAR C = Text[I];
				if (I == 0 && C == 0xFEFF) { continue; }
				if (Quoted)
				{
					if (C == '"')
					{
						if (I + 1 < Text.Len() && Text[I + 1] == '"') { Cell.AppendChar('"'); ++I; }
						else { Quoted = false; Closed = true; }
					}
					else { Cell.AppendChar(C); }
				}
				else if (C == ',') { Row.Add(Cell); Cell.Empty(); Closed = false; }
				else if (C == '\r' || C == '\n')
				{
					FinishRow(); if (C == '\r' && I + 1 < Text.Len() && Text[I + 1] == '\n') { ++I; }
				}
				else if (C == '"' && Cell.IsEmpty() && !Closed) { Quoted = true; }
				else if (Closed || C == '"') { Error = TEXT("Malformed CSV quoting: ") + Path; return false; }
				else { Cell.AppendChar(C); }
			}
			if (Quoted) { Error = TEXT("Unclosed CSV field: ") + Path; return false; }
			if (!Cell.IsEmpty() || !Row.IsEmpty() || Closed) { FinishRow(); }
			if (Parsed.IsEmpty()) { Error = TEXT("Missing CSV header: ") + Path; return false; }
			Header = MoveTemp(Parsed[0]); Parsed.RemoveAt(0); Rows = MoveTemp(Parsed);
			TSet<FString> Unique;
			for (const FString& Name : Header)
			{
				if (Name.IsEmpty() || Unique.Contains(Name)) { Error = TEXT("Invalid CSV header: ") + Path; return false; }
				Unique.Add(Name);
			}
			for (const FString& Name : Required) { if (!Unique.Contains(Name)) { Error = TEXT("Missing CSV column ") + Name + TEXT(": ") + Path; return false; } }
			for (const TArray<FString>& R : Rows) { if (R.Num() != Header.Num()) { Error = TEXT("CSV row width mismatch: ") + Path; return false; } }
			return true;
		}
		const FString& Get(const TArray<FString>& Row, const TCHAR* Name) const { return Row[Header.IndexOfByKey(FString(Name))]; }
		double Number(const TArray<FString>& Row, const TCHAR* Name, FString& Error) const
		{
			const FString& Value = Get(Row, Name);
			int32 I = 0, Digits = 0;
			if (I < Value.Len() && (Value[I] == '+' || Value[I] == '-')) { ++I; }
			while (I < Value.Len() && FChar::IsDigit(Value[I])) { ++I; ++Digits; }
			if (I < Value.Len() && Value[I] == '.') { ++I; while (I < Value.Len() && FChar::IsDigit(Value[I])) { ++I; ++Digits; } }
			bool Valid = Digits > 0;
			if (I < Value.Len() && (Value[I] == 'e' || Value[I] == 'E'))
			{
				++I; if (I < Value.Len() && (Value[I] == '+' || Value[I] == '-')) { ++I; }
				const int32 Start = I; while (I < Value.Len() && FChar::IsDigit(Value[I])) { ++I; }
				Valid &= I > Start;
			}
			const double Result = FCString::Atod(*Value);
			if (!Valid || I != Value.Len() || !FMath::IsFinite(Result)) { Error = FString(TEXT("Invalid finite number in column ")) + Name; }
			return Result;
		}
	};
	bool LoadJson(const FString& File, TSharedPtr<FJsonObject>& Object, FString& Error)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *File) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Object) || !Object)
		{ Error = TEXT("Cannot read JSON: ") + File; return false; }
		return true;
	}
	bool SaveJson(const FString& File, const TSharedRef<FJsonObject>& Object, FString& Error)
	{
		FString Text;
		if (!FJsonSerializer::Serialize(Object, TJsonWriterFactory<>::Create(&Text)) ||
			!FFileHelper::SaveStringToFile(Text, *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{ Error = TEXT("Cannot write JSON: ") + File; return false; }
		return true;
	}
	bool SaveChart(const FString& File, const TSharedRef<FJsonObject>& Data, FString& Error)
	{
		FString Json;
		if (!FJsonSerializer::Serialize(Data, TJsonWriterFactory<>::Create(&Json))) { Error = TEXT("Cannot serialize chart data"); return false; }
		// Keep user-editable identifiers safely inside JSON, rather than interpreting them as HTML/script.
		Json.ReplaceInline(TEXT("<"), TEXT("\\u003c")); Json.ReplaceInline(TEXT(">"), TEXT("\\u003e"));
		FString Html = TEXT(R"SHARP(<!doctype html><html lang="ko"><meta charset="utf-8"><meta name="viewport" content="width=device-width">
<title>SharpMovement 기록</title><style>body{font:15px system-ui;margin:24px;background:#151a23;color:#eef2f7}canvas{width:100%;height:460px;background:#1e2633;border-radius:8px}select{padding:8px}p{color:#becbdd}#info{min-height:40px;white-space:pre-wrap}</style>
<h1 id="title"></h1><p id="caption"></p><select id="metric"><option value="MeshErrors.MeanCm">Mesh 평균 오차 (cm)</option><option value="MeshErrors.PeakCm">Mesh 최대 오차 (cm)</option><option value="CapsuleErrors.MeanCm">Capsule 평균 오차 (cm)</option><option value="CapsuleErrors.PeakCm">Capsule 최대 오차 (cm)</option><option value="delay">첫 상태 수신 평균 추정 지연 (s)</option></select><p id="info">그래프 위에 마우스를 올려 값을 확인하세요.</p><canvas id="chart"></canvas>
<script>const data=__SHARP_DATA__;const canvas=document.getElementById('chart'),ctx=canvas.getContext('2d'),metric=document.getElementById('metric'),info=document.getElementById('info');let hits=[];
const isReport=data.Kind==='Report';document.getElementById('title').textContent=isReport?'시간에 따른 Capsule / Mesh 오차':'실측 전송량과 오차 비교';
document.getElementById('caption').textContent=isReport?'청록: Capsule, 주황: Mesh. 세로선: 관측된 급변(회색) / 첫 상태 수신(녹색). 표본 공백은 연결하지 않습니다.':'동일 조건과 예산 범위 여부는 comparison.json의 쌍을 확인하세요. 연결 전체 비용과 Actor 비용은 서로 구분해야 합니다.';
if(isReport)metric.style.display='none';
function value(r){if(metric.value==='delay'){const a=(r.Events||[]).map(e=>e.EstimatedFirstStateDelaySeconds).filter(Number.isFinite);return a.length?a.reduce((s,v)=>s+v,0)/a.length:null}const [o,k]=metric.value.split('.');return r[o]?.[k]}
function draw(){const rect=canvas.getBoundingClientRect(),w=rect.width,h=rect.height,dpr=devicePixelRatio||1;canvas.width=w*dpr;canvas.height=h*dpr;ctx.setTransform(dpr,0,0,dpr,0,0);ctx.clearRect(0,0,w,h);hits=[];
const pad=65,plotW=Math.max(1,w-pad*2),plotH=Math.max(1,h-pad*2);let pts=[];
if(isReport){for(const r of data.Samples)pts.push({x:r[0],y:r[1],z:r[2],connected:r[3]})}else{for(let i=0;i<data.Reports.length;i++){const r=data.Reports[i],y=value(r);if(Number.isFinite(r.BudgetBytesPerSecond)&&Number.isFinite(y))pts.push({x:r.BudgetBytesPerSecond,y,report:r,index:i})}}
if(!pts.length){ctx.fillStyle='#fff';ctx.fillText('사용 가능한 측정값 없음',pad,pad);return}
let xmin=pts[0].x,xmax=xmin,ymin=0,ymax=1;for(const p of pts){xmin=Math.min(xmin,p.x);xmax=Math.max(xmax,p.x);ymin=Math.min(ymin,p.y);ymax=Math.max(ymax,p.y,p.z||0)}if(xmax<=xmin)xmax=xmin+1;ymax*=1.05;
const x=v=>pad+(v-xmin)/(xmax-xmin)*plotW,y=v=>h-pad-(v-ymin)/(ymax-ymin)*plotH;
ctx.font='12px system-ui';ctx.strokeStyle='#627188';ctx.fillStyle='#d2dfef';ctx.beginPath();ctx.moveTo(pad,pad);ctx.lineTo(pad,h-pad);ctx.lineTo(w-pad,h-pad);ctx.stroke();
for(let i=0;i<=5;i++){const tx=xmin+(xmax-xmin)*i/5,ty=ymin+(ymax-ymin)*i/5;ctx.fillText(tx.toFixed(isReport?2:0),x(tx)-15,h-pad+22);ctx.fillText(ty.toFixed(2),7,y(ty)+4)}
ctx.fillText(isReport?'추정 서버 시각 (s)':'실측 bytes/s — 비용 단위는 hover에서 확인',pad,h-10);
if(isReport){for(const e of data.Report.Events||[]){for(const [t,c]of[[e.GroundTruthServerTime,'#8090a5'],[e.FirstStateReceiveServerTime,'#6fd6a0']]){if(!Number.isFinite(t)||t<xmin||t>xmax)continue;ctx.strokeStyle=c;ctx.setLineDash([4,5]);ctx.beginPath();ctx.moveTo(x(t),pad);ctx.lineTo(x(t),h-pad);ctx.stroke()}}ctx.setLineDash([]);
for(const [key,color]of[['y','#66dbe6'],['z','#ffac68']]){ctx.strokeStyle=color;ctx.beginPath();for(let i=0;i<pts.length;i++){const p=pts[i];if(i===0||!p.connected)ctx.moveTo(x(p.x),y(p[key]));else ctx.lineTo(x(p.x),y(p[key]))}ctx.stroke()}
for(const p of pts)hits.push({x:x(p.x),y:y(p.y),text:'시각 '+p.x.toFixed(4)+' s / Capsule '+p.y.toFixed(3)+' cm / Mesh '+p.z.toFixed(3)+' cm'});
}else{for(const p of pts){ctx.fillStyle='hsl('+((p.report.Mode||'').split('').reduce((a,c)=>a+c.charCodeAt(0),0)*13%360)+' 75% 70%)';ctx.beginPath();ctx.arc(x(p.x),y(p.y),5,0,Math.PI*2);ctx.fill();ctx.fillText(p.report.Mode||'Unknown',x(p.x)+8,y(p.y)-6);hits.push({x:x(p.x),y:y(p.y),text:p.report.Mode+' | '+p.report.RunId+' / Trial '+p.report.TrialId+'\n'+p.x.toFixed(2)+' bytes/s ('+p.report.BudgetScope+') / '+p.y.toFixed(4)})}}}
canvas.addEventListener('pointermove',e=>{const b=canvas.getBoundingClientRect(),px=e.clientX-b.left,py=e.clientY-b.top;let nearest=null,best=Infinity;for(const h of hits){const d=isReport?Math.abs(px-h.x):Math.hypot(px-h.x,py-h.y);if(d<best){best=d;nearest=h}}if(nearest)info.textContent=nearest.text});metric.addEventListener('change',draw);window.addEventListener('resize',draw);draw();</script></html>)SHARP");
		Html.ReplaceInline(TEXT("__SHARP_DATA__"), *Json);
		if (!FFileHelper::SaveStringToFile(Html, *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)) { Error = TEXT("Cannot write chart: ") + File; return false; }
		return true;
	}
	FString Csv(FString Value) { Value.ReplaceInline(TEXT("\""), TEXT("\"\"")); return TEXT("\"") + Value + TEXT("\""); }
	void Nullable(const TSharedRef<FJsonObject>& Obj, const FString& Name, double Value)
	{
		if (Value >= 0.0 && FMath::IsFinite(Value)) { Obj->SetNumberField(Name, Value); }
		else { Obj->SetField(Name, MakeShared<FJsonValueNull>()); }
	}
	TSharedPtr<FJsonValue> Field(const TSharedPtr<FJsonObject>& Obj, const FString& Name)
	{
		if (Obj) { for (const auto& Pair : Obj->Values) { if (FString(*Pair.Key).Equals(Name, ESearchCase::IgnoreCase)) { return Pair.Value; } } }
		return nullptr;
	}
	// Stable ordering makes the comparison key independent of TMap iteration order.
	FString Canonical(const TSharedPtr<FJsonValue>& Value)
	{
		if (!Value) { return TEXT("null"); }
		if (Value->Type == EJson::Object)
		{
			TArray<FString> Keys; for (const auto& Pair : Value->AsObject()->Values) { Keys.Add(FString(*Pair.Key)); } Keys.Sort();
			TArray<FString> Parts;
			for (const FString& Key : Keys) { Parts.Add(Key + TEXT(":") + Canonical(Value->AsObject()->TryGetField(Key))); }
			return TEXT("{") + FString::Join(Parts, TEXT(",")) + TEXT("}");
		}
		if (Value->Type == EJson::Array)
		{
			TArray<FString> Parts; for (const auto& V : Value->AsArray()) { Parts.Add(Canonical(V)); }
			return TEXT("[") + FString::Join(Parts, TEXT(",")) + TEXT("]");
		}
		if (Value->Type == EJson::String) { return Csv(Value->AsString()); }
		if (Value->Type == EJson::Number) { return FString::Printf(TEXT("%.17g"), Value->AsNumber()); }
		if (Value->Type == EJson::Boolean) { return Value->AsBool() ? TEXT("true") : TEXT("false"); }
		return TEXT("null");
	}
	struct FIdentity
	{
		FString Run, Actor, Trial;
		bool Validate(const FTable& Table, FString& Error)
		{
			for (const auto& Row : Table.Rows)
			{
				if (Run.IsEmpty()) { Run = Table.Get(Row, TEXT("RunId")); Trial = Table.Get(Row, TEXT("TrialId")); Actor = Table.Get(Row, TEXT("ActorId")); }
				if (Run.IsEmpty() || Actor.IsEmpty() || Run != Table.Get(Row, TEXT("RunId")) || Trial != Table.Get(Row, TEXT("TrialId")) || Actor != Table.Get(Row, TEXT("ActorId")))
				{ Error = TEXT("Recordings must have the same RunId, TrialId and ActorId"); return false; }
			}
			return true;
		}
	};
	struct FSample { double Time = 0.0, LocalTime = 0.0; FVector Position, Mesh; };
	bool ReadSamples(const FString& File, bool Client, TArray<FSample>& Samples, FIdentity& Identity, FString& Error)
	{
		FTable Table;
		TArray<FString> Required = {TEXT("RunId"), TEXT("TrialId"), TEXT("ActorId"), TEXT("ServerWorldTime"), TEXT("PositionX"), TEXT("PositionY"), TEXT("PositionZ"), TEXT("MeshX"), TEXT("MeshY"), TEXT("MeshZ")};
		if (Client) { Required.Add(TEXT("LocalWorldTime")); }
		if (!Table.Load(File, Required, Error) || !Identity.Validate(Table, Error)) { return false; }
		for (const auto& Row : Table.Rows)
		{
			FSample S;
			S.Time = Table.Number(Row, TEXT("ServerWorldTime"), Error);
			S.LocalTime = Client ? Table.Number(Row, TEXT("LocalWorldTime"), Error) : S.Time;
			S.Position = FVector(Table.Number(Row, TEXT("PositionX"), Error), Table.Number(Row, TEXT("PositionY"), Error), Table.Number(Row, TEXT("PositionZ"), Error));
			S.Mesh = FVector(Table.Number(Row, TEXT("MeshX"), Error), Table.Number(Row, TEXT("MeshY"), Error), Table.Number(Row, TEXT("MeshZ"), Error));
			if (!Error.IsEmpty()) { return false; }
			if (!Client && !Samples.IsEmpty())
			{
				if (S.Time < Samples.Last().Time) { Error = TEXT("Server clock moved backwards within a Trial"); return false; }
				if (S.Time == Samples.Last().Time) { Samples.Last() = S; continue; }
			}
			Samples.Add(S);
		}
		if (Samples.Num() < 2) { Error = TEXT("At least two samples per side are required"); return false; }
		return true;
	}
	bool Interpolate(const TArray<FSample>& Server, double Time, double MaxGap, FSample& Result)
	{
		if (Time < Server[0].Time || Time > Server.Last().Time) { return false; }
		int32 Low = 0, High = Server.Num() - 1;
		while (Low < High) { const int32 Mid = (Low + High) / 2; if (Server[Mid].Time < Time) { Low = Mid + 1; } else { High = Mid; } }
		if (Server[Low].Time == Time) { Result = Server[Low]; return true; }
		if (Low == 0 || Server[Low].Time - Server[Low - 1].Time > MaxGap) { return false; }
		const FSample& A = Server[Low - 1]; const FSample& B = Server[Low];
		const double Alpha = (Time - A.Time) / (B.Time - A.Time);
		Result.Time = Time; Result.Position = FMath::Lerp(A.Position, B.Position, Alpha); Result.Mesh = FMath::Lerp(A.Mesh, B.Mesh, Alpha); return true;
	}
	struct FPair { double Time, CapsuleError, MeshError; bool Connected; };
	TSharedRef<FJsonObject> Errors(const TArray<FPair>& Pairs, double Begin, double End, bool Mesh)
	{
		auto Result = MakeShared<FJsonObject>();
		double Peak = -1.0, Area = 0.0, Duration = 0.0;
		int32 Count = 0;
		for (int32 I = 0; I < Pairs.Num(); ++I)
		{
			const FPair& P = Pairs[I];
			const double E = Mesh ? P.MeshError : P.CapsuleError;
			if (P.Time >= Begin && P.Time <= End) { Peak = FMath::Max(Peak, E); ++Count; }
			if (I == 0 || !P.Connected) { continue; }
			const FPair& A = Pairs[I - 1];
			const double Left = FMath::Max(Begin, A.Time), Right = FMath::Min(End, P.Time);
			if (Right <= Left) { continue; }
			const double EA = Mesh ? A.MeshError : A.CapsuleError;
			const double EL = FMath::Lerp(EA, E, (Left - A.Time) / (P.Time - A.Time));
			const double ER = FMath::Lerp(EA, E, (Right - A.Time) / (P.Time - A.Time));
			Peak = FMath::Max(Peak, FMath::Max(EL, ER)); Area += (EL + ER) * 0.5 * (Right - Left); Duration += Right - Left;
		}
		Nullable(Result, TEXT("PeakCm"), Peak); Nullable(Result, TEXT("MeanCm"), Duration > 0.0 ? Area / Duration : -1.0);
		Nullable(Result, TEXT("AreaCmSeconds"), Peak >= 0.0 ? Area : -1.0);
		Result->SetNumberField(TEXT("ValidDurationSeconds"), Duration); Result->SetNumberField(TEXT("Samples"), Count);
		return Result;
	}
	double Recovery(const TArray<FPair>& Pairs, double Begin, double End, bool Mesh, const FSharpMetricsSettings& Settings)
	{
		double Peak = -1.0, PeakTime = Begin;
		for (const auto& P : Pairs)
		{
			const double E = Mesh ? P.MeshError : P.CapsuleError;
			if (P.Time >= Begin && P.Time <= End && E > Peak) { Peak = E; PeakTime = P.Time; }
		}
		if (Peak < 0.0) { return -1.0; }
		double Since = -1.0;
		for (const auto& P : Pairs)
		{
			if (P.Time < (Peak > Settings.RecoveryError ? PeakTime : Begin) || P.Time > End) { continue; }
			if (!P.Connected) { Since = -1.0; }
			if ((Mesh ? P.MeshError : P.CapsuleError) > Settings.RecoveryError) { Since = -1.0; continue; }
			if (Since < 0.0) { Since = P.Time; }
			if (P.Time - Since >= Settings.RecoveryHoldSeconds) { return Since - Begin; }
		}
		return -1.0;
	}
	struct FDiagnostic { FString Kind, Detail; double Source, Receipt, Value1, Value2; };
	bool ReadDiagnostics(const FString& File, TArray<FDiagnostic>& Entries, FIdentity& Identity, FString& Error)
	{
		FTable T;
		if (!T.Load(File, {TEXT("RunId"), TEXT("TrialId"), TEXT("ActorId"), TEXT("Kind"), TEXT("Detail"), TEXT("SourceServerTime"), TEXT("ReceiveServerTime"), TEXT("Value1"), TEXT("Value2")}, Error) || !Identity.Validate(T, Error)) { return false; }
		for (const auto& R : T.Rows)
		{
			Entries.Add({T.Get(R, TEXT("Kind")), T.Get(R, TEXT("Detail")), T.Number(R, TEXT("SourceServerTime"), Error), T.Number(R, TEXT("ReceiveServerTime"), Error),
				T.Number(R, TEXT("Value1"), Error), T.Number(R, TEXT("Value2"), Error)});
		}
		return Error.IsEmpty();
	}
	struct FTruthEvent { FString Id; double Time, CallTime; FString ObservedSampleId; };
	struct FAggregate
	{
		int32 Count = 0; double Sum = 0.0, Squares = 0.0, Minimum = DBL_MAX, Maximum = -DBL_MAX;
		void Add(double Value)
		{
			if (!FMath::IsFinite(Value)) { return; }
			++Count; Sum += Value; Squares += Value * Value; Minimum = FMath::Min(Minimum, Value); Maximum = FMath::Max(Maximum, Value);
		}
		TSharedRef<FJsonObject> Json() const
		{
			auto Obj = MakeShared<FJsonObject>(); Obj->SetNumberField(TEXT("Count"), Count);
			if (Count)
			{
				Obj->SetNumberField(TEXT("Mean"), Sum / Count); Obj->SetNumberField(TEXT("Minimum"), Minimum); Obj->SetNumberField(TEXT("Maximum"), Maximum);
				Nullable(Obj, TEXT("SampleStandardDeviation"), Count > 1 ? FMath::Sqrt(FMath::Max(0.0, (Squares - Sum * Sum / Count) / (Count - 1))) : -1.0);
			}
			return Obj;
		}
	};
	FString OutputFolder(const FString& Configured)
	{
		return FPaths::ConvertRelativePathToFull(Configured.IsEmpty() ? FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SharpMovementReports"),
			FGuid::NewGuid().ToString(EGuidFormats::Digits)) : Configured);
	}
}

bool FSharpMetricsSettings::IsValid() const
{
	const float Values[] = {MaximumInterpolationGap, EventWindowSeconds, DetectorMatchSeconds, TimestampToleranceSeconds, RecoveryError, RecoveryHoldSeconds, BudgetTolerancePercent, MinimumComparisonCoverage};
	for (float V : Values) { if (!FMath::IsFinite(V) || V < 0.0f) { return false; } }
	return MaximumInterpolationGap > 0.0f && EventWindowSeconds > 0.0f && DetectorMatchSeconds > 0.0f && BudgetTolerancePercent <= 100.0f && MinimumComparisonCoverage <= 1.0f;
}

ASharpMovementReport::ASharpMovementReport() { PrimaryActorTick.bCanEverTick = false; }

void ASharpMovementReport::GenerateReport()
{
	LastError.Empty();
	auto Fail = [this](const FString& Reason) { LastError = Reason; UE_LOG(LogSharpMetrics, Error, TEXT("%s"), *LastError); };
	if (!Metrics.IsValid()) { Fail(TEXT("Invalid Metrics settings")); return; }
	if (ServerRecordingDirectory.Path.IsEmpty() || ClientRecordingDirectory.Path.IsEmpty()) { Fail(TEXT("Set Server and Client recording directories")); return; }
	const FString ServerDir = FPaths::ConvertRelativePathToFull(ServerRecordingDirectory.Path);
	const FString ClientDir = FPaths::ConvertRelativePathToFull(ClientRecordingDirectory.Path);
	TSharedPtr<FJsonObject> ServerManifest, ClientManifest;
	if (!LoadJson(FPaths::Combine(ServerDir, TEXT("manifest.json")), ServerManifest, LastError) ||
		!LoadJson(FPaths::Combine(ClientDir, TEXT("manifest.json")), ClientManifest, LastError)) { Fail(LastError); return; }
	FString ServerStatus, ClientStatus, ServerRole, ClientRole;
	ServerManifest->TryGetStringField(TEXT("Status"), ServerStatus); ClientManifest->TryGetStringField(TEXT("Status"), ClientStatus);
	ServerManifest->TryGetStringField(TEXT("Role"), ServerRole); ClientManifest->TryGetStringField(TEXT("Role"), ClientRole);
	FString ServerError, ClientError;
	ServerManifest->TryGetStringField(TEXT("Error"), ServerError); ClientManifest->TryGetStringField(TEXT("Error"), ClientError);
	if (ServerRole != TEXT("Server") || ClientRole != TEXT("Client") || ServerStatus.IsEmpty() || ClientStatus.IsEmpty() ||
		ServerStatus == TEXT("Recording") || ClientStatus == TEXT("Recording") || !ServerError.IsEmpty() || !ClientError.IsEmpty())
	{ Fail(TEXT("Choose finished Server and Client recordings without a file error")); return; }
	FIdentity Identity;
	TArray<FSample> Server, Client;
	if (!ReadSamples(FPaths::Combine(ServerDir, TEXT("server_samples.csv")), false, Server, Identity, LastError) ||
		!ReadSamples(FPaths::Combine(ClientDir, TEXT("client_samples.csv")), true, Client, Identity, LastError)) { Fail(LastError); return; }
	TArray<FDiagnostic> ServerDiagnostics, ClientDiagnostics;
	if (!ReadDiagnostics(FPaths::Combine(ServerDir, TEXT("diagnostics.csv")), ServerDiagnostics, Identity, LastError) ||
		!ReadDiagnostics(FPaths::Combine(ClientDir, TEXT("diagnostics.csv")), ClientDiagnostics, Identity, LastError)) { Fail(LastError); return; }
	FTable EventTable;
	if (!EventTable.Load(FPaths::Combine(ServerDir, TEXT("events.csv")), {TEXT("RunId"), TEXT("TrialId"), TEXT("ActorId"), TEXT("EventId"), TEXT("Status"), TEXT("CallTime"), TEXT("ActualVelocityChangeTime"), TEXT("ObservedSampleId")}, LastError) ||
		!Identity.Validate(EventTable, LastError)) { Fail(LastError); return; }
	TArray<FTruthEvent> Events;
	int32 ObservedImpulseCalls = 0;
	for (const auto& Row : EventTable.Rows)
	{
		if (EventTable.Get(Row, TEXT("Status")) == TEXT("Observed"))
		{
			++ObservedImpulseCalls;
			FTruthEvent Event{EventTable.Get(Row, TEXT("EventId")), EventTable.Number(Row, TEXT("ActualVelocityChangeTime"), LastError),
				EventTable.Number(Row, TEXT("CallTime"), LastError), EventTable.Get(Row, TEXT("ObservedSampleId"))};
			FTruthEvent* Existing = Events.FindByPredicate([&](const FTruthEvent& E) { return E.ObservedSampleId == Event.ObservedSampleId; });
			// Several impulses consumed by one movement update form one observable sharp state.
			if (Existing) { Existing->Id += TEXT("+") + Event.Id; Existing->CallTime = FMath::Min(Existing->CallTime, Event.CallTime); }
			else { Events.Add(MoveTemp(Event)); }
		}
	}
	if (!LastError.IsEmpty()) { Fail(LastError); return; }
	Events.Sort([](const FTruthEvent& A, const FTruthEvent& B) { return A.Time < B.Time; });
	TArray<FPair> Pairs;
	TArray<FString> ErrorRows;
	TArray<TSharedPtr<FJsonValue>> PlotSamples;
	ErrorRows.Add(TEXT("ServerWorldTime,CapsuleErrorCm,MeshErrorCm,MeshFrameDisplacementCm,VisualResidualStepCm,MeshAccelerationMagnitudeCmS2,Connected\n"));
	int32 Rejected = 0, ClockDiscontinuities = 0;
	bool PreviousValid = false, HasMeshVelocity = false;
	FSample PreviousClient, PreviousTruth;
	FVector PreviousMeshVelocity = FVector::ZeroVector;
	double MaxFrameStep = 0.0, MaxResidual = 0.0, MaxMeshAcceleration = 0.0;
	double LastAlignedTime = -DBL_MAX;
	for (const auto& C : Client)
	{
		FSample Truth;
		if (C.Time <= LastAlignedTime) { ++Rejected; ++ClockDiscontinuities; PreviousValid = HasMeshVelocity = false; continue; }
		if (!Interpolate(Server, C.Time, Metrics.MaximumInterpolationGap, Truth)) { ++Rejected; PreviousValid = HasMeshVelocity = false; continue; }
		LastAlignedTime = C.Time;
		const double Dt = C.Time - PreviousClient.Time, LocalDt = C.LocalTime - PreviousClient.LocalTime;
		const bool Connected = PreviousValid && Dt > 0.0 && LocalDt > 0.0 && Dt <= Metrics.MaximumInterpolationGap && LocalDt <= Metrics.MaximumInterpolationGap;
		if (PreviousValid && (Dt <= 0.0 || LocalDt <= 0.0)) { ++ClockDiscontinuities; }
		double Step = 0.0, Residual = 0.0, Acceleration = 0.0;
		if (Connected)
		{
			const FVector Displacement = C.Mesh - PreviousClient.Mesh;
			const FVector MeshVelocity = Displacement / LocalDt;
			Step = Displacement.Size(); Residual = (Displacement - (Truth.Mesh - PreviousTruth.Mesh)).Size();
			if (HasMeshVelocity) { Acceleration = ((MeshVelocity - PreviousMeshVelocity) / LocalDt).Size(); }
			PreviousMeshVelocity = MeshVelocity; HasMeshVelocity = true;
		}
		else { HasMeshVelocity = false; }
		const double CapsuleError = (C.Position - Truth.Position).Size(), MeshError = (C.Mesh - Truth.Mesh).Size();
		Pairs.Add({C.Time, CapsuleError, MeshError, Connected});
		if (bWriteHtmlCharts)
		{
			PlotSamples.Add(MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{
				MakeShared<FJsonValueNumber>(C.Time), MakeShared<FJsonValueNumber>(CapsuleError), MakeShared<FJsonValueNumber>(MeshError), MakeShared<FJsonValueBoolean>(Connected)}));
		}
		ErrorRows.Add(FString::Printf(TEXT("%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%d\n"), C.Time, CapsuleError, MeshError, Step, Residual, Acceleration, Connected ? 1 : 0));
		MaxFrameStep = FMath::Max(MaxFrameStep, Step); MaxResidual = FMath::Max(MaxResidual, Residual); MaxMeshAcceleration = FMath::Max(MaxMeshAcceleration, Acceleration);
		PreviousClient = C; PreviousTruth = Truth; PreviousValid = true;
	}
	if (Pairs.Num() < 2) { Fail(TEXT("Insufficient time-aligned samples; check server clock and interpolation gap")); return; }
	double Begin = Server[0].Time, End = Server.Last().Time;
	TArray<double> Detections;
	double CostTotal = 0.0, CostPeak = 0.0; int32 CostCount = 0;
	for (const auto& D : ServerDiagnostics)
	{
		if (D.Kind == TEXT("DetectorEvent")) { Detections.Add(D.Source); }
		if (D.Kind == TEXT("DetectorSample")) { CostTotal += D.Value2; CostPeak = FMath::Max(CostPeak, D.Value2); ++CostCount; }
	}
	Detections.Sort(); TSet<int32> Matched;
	TArray<TSharedPtr<FJsonValue>> EventResults;
	double DelayTotal = 0.0, DelayPeak = 0.0;
	int32 TruePositives = 0;
	for (int32 I = 0; I < Events.Num(); ++I)
	{
		const FTruthEvent& Event = Events[I];
		const double EventEnd = FMath::Min(End, FMath::Min(Event.Time + Metrics.EventWindowSeconds, I + 1 < Events.Num() ? Events[I + 1].Time : End));
		auto E = MakeShared<FJsonObject>();
		E->SetStringField(TEXT("EventId"), Event.Id); E->SetNumberField(TEXT("GroundTruthServerTime"), Event.Time); E->SetNumberField(TEXT("WindowEnd"), EventEnd);
		E->SetNumberField(TEXT("ImpulseToObservedChangeSeconds"), Event.Time - Event.CallTime);
		double DetectionDelay = -1.0;
		for (int32 J = 0; J < Detections.Num(); ++J)
		{
			if (!Matched.Contains(J) && Detections[J] >= Event.Time - Metrics.TimestampToleranceSeconds && Detections[J] <= Event.Time + Metrics.DetectorMatchSeconds)
			{ Matched.Add(J); DetectionDelay = FMath::Max(0.0, Detections[J] - Event.Time); ++TruePositives; DelayTotal += DetectionDelay; DelayPeak = FMath::Max(DelayPeak, DetectionDelay); break; }
		}
		Nullable(E, TEXT("DetectionDelaySeconds"), DetectionDelay);
		double MovementReceipt = -1.0, SnapshotReceipt = -1.0;
		for (const auto& D : ClientDiagnostics)
		{
			if (D.Source < Event.Time - Metrics.TimestampToleranceSeconds || D.Source > EventEnd + Metrics.TimestampToleranceSeconds) { continue; }
			if (D.Kind == TEXT("MovementReceived") && (MovementReceipt < 0.0 || D.Receipt < MovementReceipt)) { MovementReceipt = D.Receipt; }
			if (D.Kind == TEXT("SnapshotReceived") && D.Detail == TEXT("Valid") && (SnapshotReceipt < 0.0 || D.Receipt < SnapshotReceipt)) { SnapshotReceipt = D.Receipt; }
		}
		const double First = MovementReceipt < 0.0 ? SnapshotReceipt : (SnapshotReceipt < 0.0 ? MovementReceipt : FMath::Min(MovementReceipt, SnapshotReceipt));
		Nullable(E, TEXT("FirstMovementReceiveServerTime"), MovementReceipt); Nullable(E, TEXT("FirstSnapshotReceiveServerTime"), SnapshotReceipt);
		Nullable(E, TEXT("FirstStateReceiveServerTime"), First);
		if (First >= 0.0) { E->SetNumberField(TEXT("EstimatedFirstStateDelaySeconds"), First - Event.Time); }
		else { E->SetField(TEXT("EstimatedFirstStateDelaySeconds"), MakeShared<FJsonValueNull>()); }
		for (int32 Kind = 0; Kind < 2; ++Kind)
		{
			const FString Name = Kind ? TEXT("Mesh") : TEXT("Capsule");
			E->SetObjectField(Name + TEXT("Errors"), Errors(Pairs, Event.Time, EventEnd, Kind != 0));
			const double Split = First < 0.0 ? EventEnd : FMath::Clamp(First, Event.Time, EventEnd);
			E->SetObjectField(Name + TEXT("BeforeFirstReceive"), Errors(Pairs, Event.Time, Split, Kind != 0));
			if (First >= 0.0 && First <= EventEnd) { E->SetObjectField(Name + TEXT("AfterFirstReceive"), Errors(Pairs, Split, EventEnd, Kind != 0)); }
			else { E->SetField(Name + TEXT("AfterFirstReceive"), MakeShared<FJsonValueNull>()); }
			const double R = Recovery(Pairs, Event.Time, EventEnd, Kind != 0, Metrics);
			Nullable(E, Name + TEXT("RecoverySeconds"), R); E->SetBoolField(Name + TEXT("RecoveryCensored"), R < 0.0);
		}
		EventResults.Add(MakeShared<FJsonValueObject>(E));
	}
	auto Report = MakeShared<FJsonObject>();
	auto MetricJson = MakeShared<FJsonObject>();
	FJsonObjectConverter::UStructToJsonObject(FSharpMetricsSettings::StaticStruct(), &Metrics, MetricJson, 0, 0);
	Report->SetObjectField(TEXT("MetricsSettings"), MetricJson);
	Report->SetNumberField(TEXT("SchemaVersion"), 1); Report->SetStringField(TEXT("RunId"), Identity.Run); Report->SetStringField(TEXT("TrialId"), Identity.Trial);
	Report->SetStringField(TEXT("ActorId"), Identity.Actor); Report->SetStringField(TEXT("ServerRecording"), ServerDir); Report->SetStringField(TEXT("ClientRecording"), ClientDir);
	Report->SetStringField(TEXT("ServerStatus"), ServerStatus); Report->SetStringField(TEXT("ClientStatus"), ClientStatus);
	const auto ContextValue = Field(ServerManifest, TEXT("ExperimentContext"));
	const auto Context = ContextValue ? ContextValue->AsObject() : nullptr;
	const auto IdentityValue = Field(Context, TEXT("Identity"));
	const auto Mode = Field(IdentityValue ? IdentityValue->AsObject() : nullptr, TEXT("Mode"));
	Report->SetStringField(TEXT("Mode"), Mode && Mode->Type == EJson::String ? Mode->AsString() : TEXT("Unknown"));
	auto CapsuleErrors = Errors(Pairs, Begin, End, false);
	Report->SetObjectField(TEXT("CapsuleErrors"), CapsuleErrors); Report->SetObjectField(TEXT("MeshErrors"), Errors(Pairs, Begin, End, true));
	Report->SetNumberField(TEXT("ServerTruthDurationSeconds"), End - Begin);
	Report->SetNumberField(TEXT("AlignmentCoverage"), End > Begin ? CapsuleErrors->GetNumberField(TEXT("ValidDurationSeconds")) / (End - Begin) : 0.0);
	Report->SetArrayField(TEXT("Events"), EventResults); Report->SetNumberField(TEXT("ExcludedImpulseCalls"), EventTable.Rows.Num() - ObservedImpulseCalls);
	Report->SetNumberField(TEXT("ObservedImpulseCalls"), ObservedImpulseCalls); Report->SetNumberField(TEXT("ObservableSharpStates"), Events.Num());
	Report->SetNumberField(TEXT("AlignedSamples"), Pairs.Num()); Report->SetNumberField(TEXT("RejectedSamples"), Rejected);
	Report->SetNumberField(TEXT("ClientClockDiscontinuities"), ClockDiscontinuities);
	Report->SetNumberField(TEXT("MaximumMeshFrameDisplacementCm"), MaxFrameStep); Report->SetNumberField(TEXT("MaximumVisualResidualStepCm"), MaxResidual);
	Report->SetNumberField(TEXT("MaximumMeshAccelerationCmS2"), MaxMeshAcceleration);
	auto Detector = MakeShared<FJsonObject>();
	Detector->SetNumberField(TEXT("TruePositives"), TruePositives); Detector->SetNumberField(TEXT("FalsePositives"), Detections.Num() - TruePositives);
	Detector->SetNumberField(TEXT("FalseNegatives"), Events.Num() - TruePositives);
	Nullable(Detector, TEXT("Precision"), Detections.Num() ? static_cast<double>(TruePositives) / Detections.Num() : -1.0);
	Nullable(Detector, TEXT("Recall"), Events.Num() ? static_cast<double>(TruePositives) / Events.Num() : -1.0);
	Nullable(Detector, TEXT("MeanDelaySeconds"), TruePositives ? DelayTotal / TruePositives : -1.0);
	Nullable(Detector, TEXT("WorstDelaySeconds"), TruePositives ? DelayPeak : -1.0);
	Nullable(Detector, TEXT("FalsePositivesPerMinute"), End > Begin ? (Detections.Num() - TruePositives) * 60.0 / (End - Begin) : -1.0);
	Nullable(Detector, TEXT("MeanClassifierMicroseconds"), CostCount ? CostTotal / CostCount : -1.0);
	Nullable(Detector, TEXT("WorstClassifierMicroseconds"), CostCount ? CostPeak : -1.0); Report->SetObjectField(TEXT("Detector"), Detector);
	double MaxCorrection = 0.0, IntervalTotal = 0.0, IntervalMin = DBL_MAX, IntervalMax = 0.0, LastReceipt = -1.0;
	int32 Corrections = 0, Intervals = 0, SequenceGaps = 0, PredictionStops = 0;
	TMap<FString, int32> StopReasons;
	for (const auto& D : ClientDiagnostics)
	{
		if (D.Kind == TEXT("SmoothCorrection")) { ++Corrections; MaxCorrection = FMath::Max(MaxCorrection, D.Value1); }
		if (D.Kind == TEXT("SnapshotSequenceGap")) { SequenceGaps += static_cast<int32>(D.Value1); }
		if (D.Kind == TEXT("PredictionStop")) { ++PredictionStops; ++StopReasons.FindOrAdd(D.Detail); }
		if (D.Kind == TEXT("MovementReceived"))
		{
			if (LastReceipt >= 0.0 && D.Receipt > LastReceipt) { const double Dt = D.Receipt - LastReceipt; IntervalTotal += Dt; IntervalMin = FMath::Min(IntervalMin, Dt); IntervalMax = FMath::Max(IntervalMax, Dt); ++Intervals; }
			LastReceipt = D.Receipt;
		}
	}
	auto Visual = MakeShared<FJsonObject>();
	Visual->SetNumberField(TEXT("CMCCorrectionCount"), Corrections); Visual->SetNumberField(TEXT("MaximumCMCCapsuleCorrectionCm"), MaxCorrection);
	Visual->SetNumberField(TEXT("UnobservedSnapshotSequences"), SequenceGaps); Visual->SetNumberField(TEXT("PredictionStops"), PredictionStops);
	auto Reasons = MakeShared<FJsonObject>(); for (const auto& R : StopReasons) { Reasons->SetNumberField(R.Key, R.Value); } Visual->SetObjectField(TEXT("StopReasons"), Reasons);
	Nullable(Visual, TEXT("MeanMovementReceiveIntervalSeconds"), Intervals ? IntervalTotal / Intervals : -1.0);
	Nullable(Visual, TEXT("MinimumMovementReceiveIntervalSeconds"), Intervals ? IntervalMin : -1.0);
	Nullable(Visual, TEXT("MaximumMovementReceiveIntervalSeconds"), Intervals ? IntervalMax : -1.0); Report->SetObjectField(TEXT("ClientDiagnostics"), Visual);
	FTable Connections;
	if (!Connections.Load(FPaths::Combine(ClientDir, TEXT("connections.csv")), {TEXT("RunId"), TEXT("TrialId"), TEXT("ActorId"), TEXT("PeriodSeconds"), TEXT("InBytesPerSecond"), TEXT("OutBytesPerSecond"), TEXT("InPacketsPerSecond"), TEXT("OutPacketsPerSecond")}, LastError) ||
		!Identity.Validate(Connections, LastError)) { Fail(LastError); return; }
	double Duration = 0.0, InBytes = 0.0, OutBytes = 0.0, InPackets = 0.0, OutPackets = 0.0;
	for (const auto& R : Connections.Rows)
	{
		const double Dt = Connections.Number(R, TEXT("PeriodSeconds"), LastError);
		if (Dt <= 0.0) { LastError = TEXT("Invalid connection measurement duration"); break; }
		Duration += Dt;
		InBytes += Connections.Number(R, TEXT("InBytesPerSecond"), LastError) * Dt; OutBytes += Connections.Number(R, TEXT("OutBytesPerSecond"), LastError) * Dt;
		InPackets += Connections.Number(R, TEXT("InPacketsPerSecond"), LastError) * Dt; OutPackets += Connections.Number(R, TEXT("OutPacketsPerSecond"), LastError) * Dt;
	}
	if (!LastError.IsEmpty()) { Fail(LastError); return; }
	auto Network = MakeShared<FJsonObject>();
	Network->SetStringField(TEXT("Scope"), TEXT("Engine connection counters including packet overhead, other actors and measurement metadata; not Enemy or NIC cost. Outgoing emulation drops can still be counted."));
	Network->SetNumberField(TEXT("MeasuredWallSeconds"), Duration);
	Nullable(Network, TEXT("IncomingBytesPerSecond"), Duration > 0.0 ? InBytes / Duration : -1.0); Nullable(Network, TEXT("OutgoingBytesPerSecond"), Duration > 0.0 ? OutBytes / Duration : -1.0);
	Nullable(Network, TEXT("IncomingPacketsPerSecond"), Duration > 0.0 ? InPackets / Duration : -1.0); Nullable(Network, TEXT("OutgoingPacketsPerSecond"), Duration > 0.0 ? OutPackets / Duration : -1.0);
	Report->SetObjectField(TEXT("ConnectionCost"), Network);
	double Budget = Duration > 0.0 ? InBytes / Duration : -1.0;
	FString BudgetScope = TEXT("ClientConnectionIncoming");
	if (!ActorCostCsv.FilePath.IsEmpty())
	{
		FTable Costs;
		if (!Costs.Load(ActorCostCsv.FilePath, {TEXT("RunId"), TEXT("TrialId"), TEXT("ActorId"), TEXT("ActorBytesPerSecond"), TEXT("SnapshotBytesPerSecond"), TEXT("RpcBytesPerSecond")}, LastError)) { Fail(LastError); return; }
		int32 Matching = 0;
		for (const auto& R : Costs.Rows)
		{
			if (Costs.Get(R, TEXT("RunId")) != Identity.Run || Costs.Get(R, TEXT("TrialId")) != Identity.Trial || Costs.Get(R, TEXT("ActorId")) != Identity.Actor) { continue; }
			++Matching;
			auto Cost = MakeShared<FJsonObject>();
			for (const TCHAR* Name : {TEXT("ActorBytesPerSecond"), TEXT("SnapshotBytesPerSecond"), TEXT("RpcBytesPerSecond")})
			{
				const double Value = Costs.Number(R, Name, LastError); if (Value < 0.0) { LastError = TEXT("Trace costs must be nonnegative"); }
				Cost->SetNumberField(Name, Value); if (FString(Name) == TEXT("ActorBytesPerSecond")) { Budget = Value; }
			}
			Cost->SetStringField(TEXT("Source"), FPaths::ConvertRelativePathToFull(ActorCostCsv.FilePath)); Report->SetObjectField(TEXT("ActorCostFromTrace"), Cost);
		}
		if (!LastError.IsEmpty() || Matching != 1) { Fail(LastError.IsEmpty() ? TEXT("Trace CSV needs exactly one matching Actor row per report") : LastError); return; }
		BudgetScope = TEXT("EnemyActorFromNetworkingInsights");
	}
	Nullable(Report, TEXT("BudgetBytesPerSecond"), Budget); Report->SetStringField(TEXT("BudgetScope"), BudgetScope);
	auto ComparisonKey = MakeShared<FJsonObject>();
	auto EvaluationSettings = MakeShared<FJsonObject>();
	for (const auto& Setting : MetricJson->Values)
	{
		const FString Name(*Setting.Key);
		if (!Name.Equals(TEXT("BudgetTolerancePercent"), ESearchCase::IgnoreCase) && !Name.Equals(TEXT("MinimumComparisonCoverage"), ESearchCase::IgnoreCase)) { EvaluationSettings->SetField(Setting.Key, Setting.Value); }
	}
	ComparisonKey->SetObjectField(TEXT("MetricsSettings"), EvaluationSettings);
	for (const TCHAR* Name : {TEXT("EngineVersion"), TEXT("MapIdentity"), TEXT("FollowSettings"), TEXT("Volumes"), TEXT("PathTransform"), TEXT("PathLength"), TEXT("PathGeometry"), TEXT("EnemyCount"), TEXT("NetworkEmulation"), TEXT("FrameRateLimit")})
	{ if (auto V = Field(ServerManifest, Name)) { ComparisonKey->SetField(Name, V); } }
	if (auto V = Field(ClientManifest, TEXT("NetworkEmulation"))) { ComparisonKey->SetField(TEXT("ClientNetworkEmulation"), V); }
	if (auto V = Field(ClientManifest, TEXT("FrameRateLimit"))) { ComparisonKey->SetField(TEXT("ClientFrameRateLimit"), V); }
	for (const TCHAR* Name : {TEXT("PathId"), TEXT("PathRevision")}) { if (auto V = Field(Context, Name)) { ComparisonKey->SetField(Name, V); } }
	auto PhysicsValue = Field(ServerManifest, TEXT("MovementSettings"));
	if (PhysicsValue)
	{
		auto Physics = MakeShared<FJsonObject>();
		for (const auto& P : PhysicsValue->AsObject()->Values)
		{
			if (P.Key != TEXT("NetUpdateFrequency") && P.Key != TEXT("MinNetUpdateFrequency") && P.Key != TEXT("NetworkSmoothingMode")) { Physics->SetField(P.Key, P.Value); }
		}
		ComparisonKey->SetObjectField(TEXT("MovementPhysics"), Physics);
	}
	auto Recording = Field(ServerManifest, TEXT("RecordingSettings"));
	if (Recording)
	{
		auto Measurement = MakeShared<FJsonObject>();
		for (const auto& P : Recording->AsObject()->Values)
		{
			if (!FString(*P.Key).Equals(TEXT("OutputSubdirectory"), ESearchCase::IgnoreCase)) { Measurement->SetField(P.Key, P.Value); }
		}
		ComparisonKey->SetObjectField(TEXT("RecordingSettings"), Measurement);
	}
	Report->SetStringField(TEXT("ComparisonKey"), Canonical(MakeShared<FJsonValueObject>(ComparisonKey)));
	Report->SetStringField(TEXT("Interpretation"), TEXT("Errors use GameState estimated server time and bounded interpolation. Frame displacement includes normal motion; residual step includes prediction/clock error. Correction distance is the CMC Capsule correction, not visible Mesh retraction. FP/FN use observed impulse velocity changes as truth, so normal sharp turns/stops are negative controls. Null means unavailable; recovery can be censored. Compare videos and Networking Insights before drawing conclusions."));
	LastOutputDirectory = OutputFolder(ReportOutputDirectory.Path);
	if (!IFileManager::Get().MakeDirectory(*LastOutputDirectory, true) ||
		!FFileHelper::SaveStringToFile(FString::Join(ErrorRows, TEXT("")), *FPaths::Combine(LastOutputDirectory, TEXT("aligned_errors.csv")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM) ||
		!SaveJson(FPaths::Combine(LastOutputDirectory, TEXT("report.json")), Report, LastError))
	{ Fail(LastError.IsEmpty() ? TEXT("Cannot write report output") : LastError); return; }
	if (bWriteHtmlCharts)
	{
		auto Chart = MakeShared<FJsonObject>(); Chart->SetStringField(TEXT("Kind"), TEXT("Report"));
		Chart->SetObjectField(TEXT("Report"), Report); Chart->SetArrayField(TEXT("Samples"), PlotSamples);
		if (!SaveChart(FPaths::Combine(LastOutputDirectory, TEXT("report.html")), Chart, LastError)) { Fail(LastError); return; }
	}
	UE_LOG(LogSharpMetrics, Display, TEXT("Report written: %s"), *LastOutputDirectory);
}

void ASharpMovementReport::CompareReports()
{
	LastError.Empty();
	if (!Metrics.IsValid() || ComparisonReports.Num() < 2) { LastError = TEXT("Choose at least two reports and valid Metrics settings"); return; }
	TArray<TSharedPtr<FJsonObject>> Reports;
	TSet<FString> Unique;
	for (const auto& Directory : ComparisonReports)
	{
		const FString File = FPaths::ConvertRelativePathToFull(FPaths::Combine(Directory.Path, TEXT("report.json")));
		if (Unique.Contains(File)) { LastError = TEXT("Comparison contains a duplicate report directory"); return; }
		Unique.Add(File);
		TSharedPtr<FJsonObject> Report;
		if (!LoadJson(File, Report, LastError)) { return; }
		Reports.Add(Report);
	}
	auto Result = MakeShared<FJsonObject>(); TArray<TSharedPtr<FJsonValue>> Comparisons;
	TArray<TSharedPtr<FJsonValue>> InputReports;
	for (const auto& R : Reports) { InputReports.Add(MakeShared<FJsonValueObject>(R)); }
	Result->SetArrayField(TEXT("Reports"), InputReports);
	TMap<FString, TArray<TSharedPtr<FJsonObject>>> Groups;
	for (const auto& R : Reports)
	{
		FString Status, ModeName, Key, Scope; double Coverage;
		if (R->TryGetStringField(TEXT("ServerStatus"), Status) && Status == TEXT("Completed") &&
			R->TryGetNumberField(TEXT("AlignmentCoverage"), Coverage) && Coverage >= Metrics.MinimumComparisonCoverage &&
			R->TryGetStringField(TEXT("Mode"), ModeName) && R->TryGetStringField(TEXT("ComparisonKey"), Key) && R->TryGetStringField(TEXT("BudgetScope"), Scope))
		{ Groups.FindOrAdd(ModeName + TEXT("|") + Scope + TEXT("|") + Key).Add(R); }
	}
	TArray<TSharedPtr<FJsonValue>> GroupSummaries;
	for (const auto& G : Groups)
	{
		auto Summary = MakeShared<FJsonObject>(); TMap<FString, FAggregate> Values;
		Summary->SetField(TEXT("Mode"), G.Value[0]->TryGetField(TEXT("Mode")));
		Summary->SetField(TEXT("BudgetScope"), G.Value[0]->TryGetField(TEXT("BudgetScope")));
		Summary->SetField(TEXT("ComparisonKey"), G.Value[0]->TryGetField(TEXT("ComparisonKey")));
		Summary->SetNumberField(TEXT("Recordings"), G.Value.Num());
		TSet<FString> ActorTrials;
		for (const auto& R : G.Value)
		{
			double Value;
			ActorTrials.Add(R->GetStringField(TEXT("RunId")) + TEXT("|") + R->GetStringField(TEXT("TrialId")) + TEXT("|") + R->GetStringField(TEXT("ActorId")));
			if (R->TryGetNumberField(TEXT("BudgetBytesPerSecond"), Value)) { Values.FindOrAdd(TEXT("BudgetBytesPerSecond")).Add(Value); }
			for (const TCHAR* Name : {TEXT("CapsuleErrors"), TEXT("MeshErrors")})
			{
				const TSharedPtr<FJsonObject>* ErrorsObject = nullptr;
				if (R->TryGetObjectField(Name, ErrorsObject))
				{
					for (const TCHAR* Metric : {TEXT("MeanCm"), TEXT("PeakCm"), TEXT("AreaCmSeconds")})
					{
						if ((*ErrorsObject)->TryGetNumberField(Metric, Value)) { Values.FindOrAdd(FString(Name) + TEXT(".") + Metric).Add(Value); }
					}
				}
			}
			const TSharedPtr<FJsonObject>* DetectorObject = nullptr;
			if (R->TryGetObjectField(TEXT("Detector"), DetectorObject))
			{
				for (const TCHAR* Name : {TEXT("Precision"), TEXT("Recall"), TEXT("MeanDelaySeconds"), TEXT("WorstDelaySeconds"), TEXT("FalsePositivesPerMinute"), TEXT("MeanClassifierMicroseconds")})
				{ if ((*DetectorObject)->TryGetNumberField(Name, Value)) { Values.FindOrAdd(FString(TEXT("Detector.")) + Name).Add(Value); } }
			}
			for (const TCHAR* Name : {TEXT("MaximumMeshFrameDisplacementCm"), TEXT("MaximumVisualResidualStepCm"), TEXT("MaximumMeshAccelerationCmS2")})
			{ if (R->TryGetNumberField(Name, Value)) { Values.FindOrAdd(Name).Add(Value); } }
		}
		Summary->SetNumberField(TEXT("DistinctActorTrials"), ActorTrials.Num());
		for (const auto& V : Values) { Summary->SetObjectField(V.Key, V.Value.Json()); }
		GroupSummaries.Add(MakeShared<FJsonValueObject>(Summary));
	}
	Result->SetArrayField(TEXT("RepeatStatisticsByModeScenarioAndScope"), GroupSummaries);
	for (int32 I = 0; I < Reports.Num(); ++I)
	{
		for (int32 J = I + 1; J < Reports.Num(); ++J)
		{
			const auto& A = Reports[I]; const auto& B = Reports[J];
			FString KeyA, KeyB, ScopeA, ScopeB, StatusA, StatusB; double BudgetA, BudgetB, CoverageA, CoverageB;
			if (!A->TryGetStringField(TEXT("ComparisonKey"), KeyA) || !B->TryGetStringField(TEXT("ComparisonKey"), KeyB) ||
				!A->TryGetStringField(TEXT("BudgetScope"), ScopeA) || !B->TryGetStringField(TEXT("BudgetScope"), ScopeB) ||
				!A->TryGetStringField(TEXT("ServerStatus"), StatusA) || !B->TryGetStringField(TEXT("ServerStatus"), StatusB) ||
				StatusA != TEXT("Completed") || StatusB != TEXT("Completed") || KeyA != KeyB || ScopeA != ScopeB ||
				!A->TryGetNumberField(TEXT("AlignmentCoverage"), CoverageA) || !B->TryGetNumberField(TEXT("AlignmentCoverage"), CoverageB) ||
				CoverageA < Metrics.MinimumComparisonCoverage || CoverageB < Metrics.MinimumComparisonCoverage ||
				!A->TryGetNumberField(TEXT("BudgetBytesPerSecond"), BudgetA) || !B->TryGetNumberField(TEXT("BudgetBytesPerSecond"), BudgetB) || BudgetA <= 0.0 || BudgetB <= 0.0) { continue; }
			const double Difference = FMath::Abs(BudgetA - BudgetB) / FMath::Max(BudgetA, BudgetB) * 100.0;
			auto Pair = MakeShared<FJsonObject>();
			Pair->SetNumberField(TEXT("ReportAIndex"), I); Pair->SetNumberField(TEXT("ReportBIndex"), J);
			Pair->SetNumberField(TEXT("BudgetDifferencePercent"), Difference); Pair->SetBoolField(TEXT("WithinBudgetTolerance"), Difference <= Metrics.BudgetTolerancePercent);
			Pair->SetNumberField(TEXT("BudgetABytesPerSecond"), BudgetA); Pair->SetNumberField(TEXT("BudgetBBytesPerSecond"), BudgetB);
			Pair->SetField(TEXT("ModeA"), A->TryGetField(TEXT("Mode"))); Pair->SetField(TEXT("ModeB"), B->TryGetField(TEXT("Mode")));
			Comparisons.Add(MakeShared<FJsonValueObject>(Pair));
		}
	}
	Result->SetArrayField(TEXT("PairsWithSameScenarioAndBudgetScope"), Comparisons);
	Result->SetNumberField(TEXT("BudgetTolerancePercent"), Metrics.BudgetTolerancePercent);
	Result->SetStringField(TEXT("Interpretation"), TEXT("Only compare WithinBudgetTolerance=true pairs. Connection budgets include other actors. Repeat trials and report spread/worst case; matching a budget does not prove visual improvement."));
	LastOutputDirectory = OutputFolder(ReportOutputDirectory.Path);
	if (!IFileManager::Get().MakeDirectory(*LastOutputDirectory, true)) { LastError = TEXT("Cannot create comparison output directory"); return; }
	SaveJson(FPaths::Combine(LastOutputDirectory, TEXT("comparison.json")), Result, LastError);
	if (LastError.IsEmpty() && bWriteHtmlCharts)
	{
		auto Chart = MakeShared<FJsonObject>(); Chart->SetStringField(TEXT("Kind"), TEXT("Comparison"));
		Chart->SetArrayField(TEXT("Reports"), InputReports);
		SaveChart(FPaths::Combine(LastOutputDirectory, TEXT("comparison.html")), Chart, LastError);
	}
}
