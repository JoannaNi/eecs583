; ModuleID = 'simple.bc'
target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v64:64:64-v128:128:128-a0:0:64-s0:64:64-f80:128:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

%struct._IO_FILE = type { i32, i8*, i8*, i8*, i8*, i8*, i8*, i8*, i8*, i8*, i8*, i8*, %struct._IO_marker*, %struct._IO_FILE*, i32, i32, i64, i16, i8, [1 x i8], i8*, i64, i8*, i8*, i8*, i8*, i64, i32, [20 x i8] }
%struct._IO_marker = type { %struct._IO_marker*, %struct._IO_FILE*, i32 }

@stdout = external global %struct._IO_FILE*
@.str = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1

; Function Attrs: nounwind uwtable
define i32 @main() #0 {
  %1 = alloca i32, align 4
  %in = alloca [1000 x i32], align 16
  %i = alloca i32, align 4
  %j = alloca i32, align 4
  %myfile = alloca %struct._IO_FILE*, align 8
  store i32 0, i32* %1
  store i32 0, i32* %i, align 4
  br label %2

; <label>:2                                       ; preds = %9, %0
  %3 = load i32* %i, align 4
  %4 = icmp slt i32 %3, 1000
  br i1 %4, label %5, label %12

; <label>:5                                       ; preds = %2
  %6 = load i32* %i, align 4
  %7 = sext i32 %6 to i64
  %8 = getelementptr inbounds [1000 x i32]* %in, i32 0, i64 %7
  store i32 0, i32* %8, align 4
  br label %9

; <label>:9                                       ; preds = %5
  %10 = load i32* %i, align 4
  %11 = add nsw i32 %10, 1
  store i32 %11, i32* %i, align 4
  br label %2

; <label>:12                                      ; preds = %2
  store i32 100, i32* %j, align 4
  br label %13

; <label>:13                                      ; preds = %22, %12
  %14 = load i32* %j, align 4
  %15 = icmp slt i32 %14, 1000
  br i1 %15, label %16, label %25

; <label>:16                                      ; preds = %13
  %17 = load i32* %j, align 4
  %18 = sext i32 %17 to i64
  %19 = getelementptr inbounds [1000 x i32]* %in, i32 0, i64 %18
  %20 = load i32* %19, align 4
  %21 = add nsw i32 %20, 10
  store i32 %21, i32* %19, align 4
  br label %22

; <label>:22                                      ; preds = %16
  %23 = load i32* %j, align 4
  %24 = add nsw i32 %23, 1
  store i32 %24, i32* %j, align 4
  br label %13

; <label>:25                                      ; preds = %13
  store i32 0, i32* %i, align 4
  br label %26

; <label>:26                                      ; preds = %36, %25
  %27 = load i32* %i, align 4
  %28 = icmp slt i32 %27, 1000
  br i1 %28, label %29, label %39

; <label>:29                                      ; preds = %26
  %30 = load %struct._IO_FILE** @stdout, align 8
  %31 = load i32* %i, align 4
  %32 = sext i32 %31 to i64
  %33 = getelementptr inbounds [1000 x i32]* %in, i32 0, i64 %32
  %34 = load i32* %33, align 4
  %35 = call i32 (%struct._IO_FILE*, i8*, ...)* @fprintf(%struct._IO_FILE* %30, i8* getelementptr inbounds ([4 x i8]* @.str, i32 0, i32 0), i32 %34)
  br label %36

; <label>:36                                      ; preds = %29
  %37 = load i32* %i, align 4
  %38 = add nsw i32 %37, 1
  store i32 %38, i32* %i, align 4
  br label %26

; <label>:39                                      ; preds = %26
  ret i32 1
}

declare i32 @fprintf(%struct._IO_FILE*, i8*, ...) #1

attributes #0 = { nounwind uwtable "less-precise-fpmad"="false" "no-frame-pointer-elim"="true" "no-frame-pointer-elim-non-leaf"="true" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #1 = { "less-precise-fpmad"="false" "no-frame-pointer-elim"="true" "no-frame-pointer-elim-non-leaf"="true" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "unsafe-fp-math"="false" "use-soft-float"="false" }
