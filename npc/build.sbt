ThisBuild / scalaVersion := "2.13.10"
ThisBuild / version := "0.1.0"

val chiselVersion = "3.6.0"

lazy val root = (project in file(".")).settings(
  name := "npc",
  libraryDependencies += "edu.berkeley.cs" %% "chisel3" % chiselVersion,
  addCompilerPlugin("edu.berkeley.cs" % "chisel3-plugin" % chiselVersion cross CrossVersion.full),
  scalacOptions ++= Seq("-deprecation", "-feature", "-unchecked", "-language:reflectiveCalls")
)
